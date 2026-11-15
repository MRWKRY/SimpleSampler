#pragma once

#include "DspUtil.h"

namespace fx
{
    /** Stereo / ping-pong delay with filters in the feedback path. Output is fully wet. */
    class StereoDelay
    {
    public:
        static constexpr float maxSeconds = 4.0f;

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            for (auto& l : lines) l.prepare ((int) (maxSeconds * sr) + 16);
            time = targetTime = 0.25f * (float) sr;
            reset();
        }

        void reset() noexcept
        {
            for (auto& l : lines) l.reset();
            for (auto& f : lowpass) f.reset();
            for (auto& f : highpass) f.reset();
        }

        void setParameters (float timeMs, float feedbackAmount, float lowCutHz, float highCutHz, bool pingPongMode) noexcept
        {
            targetTime = juce::jlimit (1.0f, maxSeconds * (float) sr - 8.0f, timeMs * 0.001f * (float) sr);
            feedback = juce::jlimit (0.0f, 0.97f, feedbackAmount);
            pingPong = pingPongMode;
            for (int ch = 0; ch < 2; ++ch)
            {
                highpass[ch].setCutoff (lowCutHz, sr);
                lowpass[ch].setCutoff (highCutHz, sr);
            }
        }

        /** Jumps straight to the target time (used right after prepare / reset). */
        void snapTime() noexcept                        { time = targetTime; }

        void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                // tape-style glide when the time changes (no clicks)
                time += (targetTime - time) * 0.00035f;

                const float dl = lines[0].read (time);
                const float dr = lines[1].read (time);
                const float fl = softLimit (highpass[0].highpass (lowpass[0].lowpass (dl))) * feedback;
                const float fr = softLimit (highpass[1].highpass (lowpass[1].lowpass (dr))) * feedback;

                if (pingPong)
                {
                    lines[0].write (0.5f * (inL[i] + inR[i]) + fr);   // left repeats first ...
                    lines[1].write (fl);                               // ... then crosses to the right
                }
                else
                {
                    lines[0].write (inL[i] + fl);
                    lines[1].write (inR[i] + fr);
                }

                outL[i] = dl;
                outR[i] = dr;
            }
        }

    private:
        static float softLimit (float x) noexcept
        {
            const float a = std::abs (x);
            return a <= 1.0f ? x : (x > 0.0f ? 1.0f : -1.0f) * (1.0f + std::tanh (a - 1.0f));
        }

        double sr = 44100.0;
        DelayLine lines[2];
        OnePole lowpass[2], highpass[2];
        float time = 1000.0f, targetTime = 1000.0f, feedback = 0.3f;
        bool pingPong = false;
    };
}
