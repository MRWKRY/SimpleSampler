#pragma once

#include "DspUtil.h"
#include "FdnReverb.h"
#include "StereoDelay.h"
#include "Botanica.h"
#include "Limiter.h"

namespace fx
{
    /** Current (already modulated) values of the global effects, in parameter units. */
    struct GlobalValues
    {
        float rvSize = 50, rvDecay = 2.5f, rvPreDelay = 20, rvLowCut = 120, rvHighCut = 9000, rvMix = 70;
        float dlTimeMs = 250, dlFeedback = 40, dlLowCut = 150, dlHighCut = 6000, dlMix = 70;
        bool dlPingPong = false;
        float masterDb = 0.0f;
    };

    /** The shared send effects (Reverb, Delay, Bloom) and the protected master output.
        Each effect only runs while it has input or a tail, so unused effects cost nothing. */
    class GlobalFx
    {
    public:
        static constexpr int maxSegment = 64;

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            reverb.prepare (sr);
            delay.prepare (sr);
            bloom.prepare (sr);
            limiter.prepare (sr);
            preDelay[0].prepare ((int) (0.26 * sr));
            preDelay[1].prepare ((int) (0.26 * sr));
            reset();
        }

        float getLimiterGain() const noexcept           { return limiter.getGainReduction(); }

        void reset() noexcept
        {
            reverb.reset(); delay.reset(); bloom.reset();
            preDelay[0].reset(); preDelay[1].reset();
            for (auto& f : returnLow) f.reset();
            for (auto& f : returnHigh) f.reset();
            reverbTail = delayTail = bloomTail = 0;
            snap = true;
            delayWasOn = false;
            limiter.reset();
            preDelaySamples = -1.0f;
        }

        /** Mixes the effect returns into main, then applies the output volume and the limiter. n <= maxSegment. */
        void process (float* mainL, float* mainR,
                      const float* revL, const float* revR, const float* dlyL, const float* dlyR,
                      const float* bloomL, const float* bloomR, int n, const GlobalValues& v) noexcept
        {
            const float slow = smoothCoef (n, 0.02f, sr);
            const float targets[3] = { juce::jlimit (0.0f, 1.0f, v.rvMix * 0.01f), juce::jlimit (0.0f, 1.0f, v.dlMix * 0.01f),
                                       dbToGain (v.masterDb) };
            if (snap) { for (int i = 0; i < 3; ++i) ramps[i].snap (targets[i]); snap = false; }
            for (int i = 0; i < 3; ++i) ramps[i].setTarget (targets[i], n, slow);

            float wetL[maxSegment], wetR[maxSegment];

            // ---- reverb (+ bloom, which shares the return tone filters)
            const bool reverbIn = hasSignal (revL, revR, n);
            const bool bloomIn = hasSignal (bloomL, bloomR, n);
            if (reverbIn) reverbTail = (int) ((v.rvPreDelay * 0.001f + v.rvDecay * 1.6f + 0.6f) * (float) sr);
            if (bloomIn)  bloomTail = (int) (14.0 * sr);

            const bool reverbOn = reverbTail > 0, bloomOn = bloomTail > 0;
            if (reverbOn || bloomOn)
            {
                float sumL[maxSegment] = {}, sumR[maxSegment] = {};

                if (reverbOn)
                {
                    reverbTail -= n;
                    // internal damping follows the high cut a little, so "dark" settings also decay darker
                    reverb.setParameters (v.rvSize * 0.01f, v.rvDecay, juce::jlimit (2500.0f, 12000.0f, v.rvHighCut * 0.9f));

                    const float wantedPre = juce::jlimit (0.0f, 0.25f * (float) sr, v.rvPreDelay * 0.001f * (float) sr);
                    if (preDelaySamples < 0.0f) preDelaySamples = wantedPre;

                    auto& mix = ramps[0];
                    for (int i = 0; i < n; ++i)
                    {
                        preDelaySamples += (wantedPre - preDelaySamples) * 0.0005f;
                        preDelay[0].write (revL[i]);
                        preDelay[1].write (revR[i]);
                        float l, r;
                        reverb.processSample (preDelay[0].read (preDelaySamples), preDelay[1].read (preDelaySamples), l, r);
                        const float m = mix.next();
                        sumL[i] = l * m;
                        sumR[i] = r * m;
                    }
                }
                else
                {
                    ramps[0].skip (n);
                }

                if (bloomOn)
                {
                    bloomTail -= n;
                    bloom.process (bloomL, bloomR, wetL, wetR, n);
                    for (int i = 0; i < n; ++i) { sumL[i] += wetL[i]; sumR[i] += wetR[i]; }
                }

                for (int ch = 0; ch < 2; ++ch)
                {
                    returnLow[ch].setCutoff (v.rvLowCut, sr);
                    returnHigh[ch].setCutoff (v.rvHighCut, sr);
                }
                for (int i = 0; i < n; ++i)
                {
                    mainL[i] += returnHigh[0].lowpass (returnLow[0].highpass (sumL[i]));
                    mainR[i] += returnHigh[1].lowpass (returnLow[1].highpass (sumR[i]));
                }
            }
            else
            {
                ramps[0].skip (n);
            }

            // ---- delay
            if (hasSignal (dlyL, dlyR, n))
            {
                const float fb = juce::jlimit (0.0f, 0.97f, v.dlFeedback * 0.01f);
                const float repeats = fb > 0.01f ? std::log (1.0e-4f) / std::log (fb) : 1.0f;   // repeats until -80 dB
                delayTail = (int) (juce::jmin (40.0f, v.dlTimeMs * 0.001f * (repeats + 1.0f) + 0.5f) * (float) sr);
            }
            if (delayTail > 0)
            {
                delayTail -= n;
                delay.setParameters (v.dlTimeMs, v.dlFeedback * 0.01f, v.dlLowCut, v.dlHighCut, v.dlPingPong);
                if (! delayWasOn) delay.snapTime();
                delay.process (dlyL, dlyR, wetL, wetR, n);
                auto& mix = ramps[1];
                for (int i = 0; i < n; ++i)
                {
                    const float m = mix.next();
                    mainL[i] += wetL[i] * m;
                    mainR[i] += wetR[i] * m;
                }
                delayWasOn = true;
            }
            else
            {
                ramps[1].skip (n);
                delayWasOn = false;
            }

            // ---- output volume + protection
            auto& master = ramps[2];
            for (int i = 0; i < n; ++i)
            {
                const float g = master.next();
                mainL[i] *= g;
                mainR[i] *= g;
            }

            if (! limiter.process (mainL, mainR, n))
                reset();    // something produced NaN / runaway values: clear every effect and start clean
        }

    private:
        static bool hasSignal (const float* l, const float* r, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
                if (std::abs (l[i]) > 1.0e-6f || std::abs (r[i]) > 1.0e-6f)
                    return true;
            return false;
        }

        double sr = 44100.0;
        FdnReverb reverb;
        StereoDelay delay;
        Bloom bloom;
        Limiter limiter;
        DelayLine preDelay[2];
        OnePole returnLow[2], returnHigh[2];
        Ramp ramps[3];
        float preDelaySamples = -1.0f;
        int reverbTail = 0, delayTail = 0, bloomTail = 0;
        bool snap = true, delayWasOn = false;
    };
}
