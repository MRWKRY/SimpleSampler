#pragma once

#include "DspUtil.h"
#include "FdnReverb.h"

// "Botanica" knobs: organic / ambient textures.
// Every effect leaves the signal untouched when its amount is 0 (callers skip it entirely).
namespace fx
{
    //==========================================================================
    /** Grain: a simplified granular texture. Short windowed grains are replayed from
        the recent past of the pad's own signal; more amount = shorter, more scattered grains. */
    class Grain
    {
    public:
        void prepare (double sampleRate)
        {
            sr = sampleRate;
            buffers[0].prepare ((int) (0.75 * sr));
            buffers[1].prepare ((int) (0.75 * sr));
            reset();
        }

        void reset() noexcept
        {
            buffers[0].reset(); buffers[1].reset();
            for (auto& g : grains) g = {};
        }

        void process (float* left, float* right, int n, Ramp& amount) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                const float a = amount.next();
                buffers[0].write (left[i]);
                buffers[1].write (right[i]);

                float wetL = 0.0f, wetR = 0.0f;
                for (auto& g : grains)
                {
                    if (g.remaining <= 0)
                    {
                        if (--g.wait > 0)
                            continue;

                        // start a new grain: size 120 ms -> 30 ms, scatter 5 ms -> 220 ms
                        const float size = (0.12f - 0.09f * a) * (0.7f + 0.6f * rng.next01());
                        g.length = juce::jmax (32, (int) (size * (float) sr));
                        g.remaining = g.length;
                        g.offset = g.length + (int) ((0.005f + 0.215f * a * rng.next01()) * (float) sr);
                        const float pan = 0.5f + 0.5f * a * rng.nextBipolar();
                        g.gainL = std::sqrt (1.0f - pan) * 1.2f;
                        g.gainR = std::sqrt (pan) * 1.2f;
                        g.wait = (int) (rng.next01() * 0.35f * (float) g.length);
                    }

                    const float phase = 1.0f - (float) g.remaining / (float) g.length;
                    const float window = 0.5f - 0.5f * std::cos (twoPi * phase);
                    // the grain reads forwards at normal speed from `offset` samples back
                    wetL += buffers[0].readInt (g.offset) * window * g.gainL;
                    wetR += buffers[1].readInt (g.offset) * window * g.gainR;
                    --g.remaining;
                }

                left[i]  = left[i]  * (1.0f - 0.6f * a) + wetL * a * 0.62f;
                right[i] = right[i] * (1.0f - 0.6f * a) + wetR * a * 0.62f;
            }
        }

    private:
        struct GrainState { int remaining = 0, length = 1, offset = 0, wait = 0; float gainL = 0.7f, gainR = 0.7f; };

        double sr = 44100.0;
        DelayLine buffers[2];
        GrainState grains[4];
        Rng rng { 0xBEEF1234u };
    };

    //==========================================================================
    /** Drift: slow random pitch and pan wander ("alive" instability). Produces control values only. */
    class DriftGenerator
    {
    public:
        explicit DriftGenerator (juce::uint32 seed = 1u) : slowPitch (seed * 7919u + 11u), fastPitch (seed * 104729u + 5u), slowPan (seed * 31337u + 3u) {}

        void advance (int numSamples, double sampleRate, float amount) noexcept
        {
            const float seconds = (float) numSamples / (float) sampleRate;
            const float slow = slowPitch.advance (seconds * 0.45f);
            const float fast = fastPitch.advance (seconds * 2.7f);
            pitchSemitones = amount * (0.38f * slow + 0.07f * fast);     // up to about +-45 cents
            pan = amount * 0.4f * slowPan.advance (seconds * 0.23f);
        }

        float pitchSemitones = 0.0f, pan = 0.0f;

    private:
        SmoothNoise slowPitch, fastPitch, slowPan;
    };

    //==========================================================================
    /** Warmth: tape-style saturation, high roll-off and a hint of wow / flutter. */
    class Warmth
    {
    public:
        void prepare (double sampleRate)
        {
            sr = sampleRate;
            delays[0].prepare ((int) (0.004 * sr) + 8);
            delays[1].prepare ((int) (0.004 * sr) + 8);
            reset();
        }
        void reset() noexcept
        {
            delays[0].reset(); delays[1].reset();
            filters[0].reset(); filters[1].reset();
        }

        void process (float* left, float* right, int n, Ramp& amount) noexcept
        {
            const float aNow = juce::jlimit (0.0f, 1.0f, amount.current);
            filters[0].setCutoff (20000.0f * std::pow (3500.0f / 20000.0f, aNow), sr);
            filters[1].a = filters[0].a;

            const float wowInc = 0.55f / (float) sr, flutterInc = 6.3f / (float) sr;

            for (int i = 0; i < n; ++i)
            {
                const float a = amount.next();
                wow += wowInc;          if (wow >= 1.0f) wow -= 1.0f;
                flutter += flutterInc;  if (flutter >= 1.0f) flutter -= 1.0f;

                // the delay shrinks to nothing as the amount goes to 0, so there is no jump at the bypass point
                const float delay = a * (float) sr * (0.0007f + 0.00045f * std::sin (twoPi * wow) + 0.00006f * std::sin (twoPi * flutter));
                const float pre = 1.0f + 2.5f * a;
                const float comp = 0.5f / std::tanh (0.5f * pre);
                const float mix = juce::jmin (1.0f, a * 8.0f);

                delays[0].write (left[i]);
                delays[1].write (right[i]);
                const float l = delays[0].read (delay), r = delays[1].read (delay);
                const float sl = std::tanh (pre * l) * comp, sr2 = std::tanh (pre * r) * comp;
                left[i]  = l + mix * (filters[0].lowpass (sl) - l);
                right[i] = r + mix * (filters[1].lowpass (sr2) - r);
            }
        }

    private:
        double sr = 44100.0;
        DelayLine delays[2];
        OnePole filters[2];
        float wow = 0.0f, flutter = 0.3f;
    };

    //==========================================================================
    /** Dew: high-frequency sparkle = exciter (harmonics made from the top end) + a chorus on the highs. */
    class Dew
    {
    public:
        void prepare (double sampleRate)
        {
            sr = sampleRate;
            for (auto& d : delays) d.prepare ((int) (0.02 * sr) + 8);
            for (int ch = 0; ch < 2; ++ch)
            {
                split[ch].setCutoff (3500.0f, sr);
                post[ch].setCutoff (5000.0f, sr);
            }
            reset();
        }
        void reset() noexcept
        {
            for (auto& d : delays) d.reset();
            for (auto& f : split) f.reset();
            for (auto& f : post) f.reset();
        }

        void process (float* left, float* right, int n, Ramp& amount) noexcept
        {
            const float inc1 = 0.71f / (float) sr, inc2 = 1.13f / (float) sr;
            for (int i = 0; i < n; ++i)
            {
                const float a = amount.next();
                lfo1 += inc1; if (lfo1 >= 1.0f) lfo1 -= 1.0f;
                lfo2 += inc2; if (lfo2 >= 1.0f) lfo2 -= 1.0f;

                const float highL = split[0].highpass (left[i]);
                const float highR = split[1].highpass (right[i]);

                // exciter: saturate the highs, keep only what is above ~5 kHz
                const float excL = post[0].highpass (std::tanh (highL * 4.0f));
                const float excR = post[1].highpass (std::tanh (highR * 4.0f));

                // chorus on the highs, different on each side
                delays[0].write (highL);
                delays[1].write (highR);
                const float chorusL = delays[0].read ((float) sr * (0.009f + 0.0022f * std::sin (twoPi * lfo1)));
                const float chorusR = delays[1].read ((float) sr * (0.012f + 0.0022f * std::sin (twoPi * lfo2)));

                left[i]  += a * (0.35f * excL + 0.6f * chorusL);
                right[i] += a * (0.35f * excR + 0.6f * chorusR);
            }
        }

    private:
        double sr = 44100.0;
        DelayLine delays[2];
        OnePole split[2], post[2];
        float lfo1 = 0.0f, lfo2 = 0.4f;
    };

    //==========================================================================
    /** Bloom: shimmer reverb on its own send bus. The send (and part of the tail) is shifted
        up one octave and fed into a long, slowly building reverb. Output is fully wet. */
    class Bloom
    {
    public:
        void prepare (double sampleRate)
        {
            sr = sampleRate;
            window = juce::jmax (256, (int) (0.045 * sr));
            for (auto& s : shifters) s.prepare (window + 8);
            reverb.prepare (sr);
            reverb.setParameters (0.92f, 7.0f, 5200.0f);
            reset();
        }

        void reset() noexcept
        {
            for (auto& s : shifters) s.reset();
            reverb.reset();
            feedbackL = feedbackR = 0.0f;
            phase = 0.0f;
        }

        void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
        {
            const float inc = 1.0f / (float) window;
            for (int i = 0; i < n; ++i)
            {
                phase += inc;
                if (phase >= 1.0f) phase -= 1.0f;

                const float upL = octaveUp (shifters[0], inL[i] + 0.2f * feedbackL);
                const float upR = octaveUp (shifters[1], inR[i] + 0.2f * feedbackR);

                float l, r;
                reverb.processSample (0.45f * inL[i] + 0.9f * upL, 0.45f * inR[i] + 0.9f * upR, l, r);
                feedbackL = l;
                feedbackR = r;
                outL[i] = l;
                outR[i] = r;
            }
        }

    private:
        /** Delay-line pitch shifter: two read heads sweep through the window at double speed and cross-fade. */
        float octaveUp (DelayLine& line, float x) noexcept
        {
            line.write (x);
            const float p2 = phase >= 0.5f ? phase - 0.5f : phase + 0.5f;
            const float g1 = 0.5f - 0.5f * std::cos (twoPi * phase);
            const float g2 = 1.0f - g1;
            return line.read ((1.0f - phase) * (float) window) * g1 + line.read ((1.0f - p2) * (float) window) * g2;
        }

        double sr = 44100.0;
        int window = 2048;
        DelayLine shifters[2];
        FdnReverb reverb;
        float feedbackL = 0.0f, feedbackR = 0.0f, phase = 0.0f;
    };
}
