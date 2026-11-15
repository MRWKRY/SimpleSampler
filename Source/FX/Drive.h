#pragma once

#include "DspUtil.h"

namespace fx
{
    /** tanh saturation with level compensation and a light 2x oversampling
        (the mid-point between two samples is shaped too, then both results are averaged). */
    class Drive
    {
    public:
        void reset() noexcept                           { prevL = prevR = 0.0f; }

        /** `amount` (0..1) is advanced once per sample. */
        void process (float* left, float* right, int n, Ramp& amount) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                const float a = amount.next();
                const float pre = 1.0f + 11.0f * a * a + 2.0f * a;
                // keep a signal peaking at 0.5 at the same peak level whatever the drive
                const float comp = 0.5f / std::tanh (0.5f * pre);
                // fades in from "untouched", so 0 is a true bypass without a jump
                const float mix = juce::jmin (1.0f, a * 12.0f);

                const float l = left[i], r = right[i];
                const float sl = 0.5f * comp * (std::tanh (pre * 0.5f * (l + prevL)) + std::tanh (pre * l));
                const float sr = 0.5f * comp * (std::tanh (pre * 0.5f * (r + prevR)) + std::tanh (pre * r));
                left[i]  = l + mix * (sl - l);
                right[i] = r + mix * (sr - r);
                prevL = l; prevR = r;
            }
        }

    private:
        float prevL = 0.0f, prevR = 0.0f;
    };
}
