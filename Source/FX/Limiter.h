#pragma once

#include "DspUtil.h"

namespace fx
{
    /** Output protection: stereo-linked peak limiter (instant attack, smooth release),
        then a hard ceiling and a NaN / runaway guard. */
    class Limiter
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            release = 1.0f - std::exp (-1.0f / (0.12f * (float) sampleRate));
            gain = 1.0f;
        }

        /** Returns false if a non-finite or absurd sample was found (the block is silenced; callers should reset their effects). */
        bool process (float* left, float* right, int n) noexcept
        {
            bool ok = true;
            for (int i = 0; i < n; ++i)
            {
                float l = left[i], r = right[i];
                if (isBad (l) || isBad (r))
                {
                    ok = false;
                    l = r = 0.0f;
                }

                const float peak = juce::jmax (std::abs (l), std::abs (r));
                const float wanted = peak > threshold ? threshold / peak : 1.0f;
                gain = wanted < gain ? wanted : gain + (wanted - gain) * release;

                left[i]  = juce::jlimit (-1.0f, 1.0f, l * gain);
                right[i] = juce::jlimit (-1.0f, 1.0f, r * gain);
            }
            return ok;
        }

        void reset() noexcept                           { gain = 1.0f; }
        float getGainReduction() const noexcept         { return gain; }

    private:
        static constexpr float threshold = 0.97f;
        float gain = 1.0f, release = 0.001f;
    };
}
