#pragma once

#include "DspUtil.h"

namespace fx
{
    /** Three-band stereo EQ: low shelf (200 Hz), sweepable mid peak, high shelf (5 kHz). */
    class Eq3
    {
    public:
        void prepare (double sampleRate)                { sr = sampleRate; reset(); lastLow = lastMid = lastFreq = lastHigh = -1000.0f; }
        void reset() noexcept                           { for (auto& ch : bands) for (auto& b : ch) b.reset(); }

        /** True when all gains are (practically) zero, i.e. processing can be skipped. */
        static bool isFlat (float lowDb, float midDb, float highDb) noexcept
        {
            return std::abs (lowDb) < 0.02f && std::abs (midDb) < 0.02f && std::abs (highDb) < 0.02f;
        }

        void setGains (float lowDb, float midDb, float midHz, float highDb) noexcept
        {
            if (std::abs (lowDb - lastLow) > 0.01f)
            {
                lastLow = lowDb;
                bands[0][0].setLowShelf (200.0f, lowDb, sr);
                bands[1][0].copyCoefficients (bands[0][0]);
            }
            if (std::abs (midDb - lastMid) > 0.01f || std::abs (midHz - lastFreq) > 0.5f)
            {
                lastMid = midDb; lastFreq = midHz;
                bands[0][1].setPeak (midHz, 0.9f, midDb, sr);
                bands[1][1].copyCoefficients (bands[0][1]);
            }
            if (std::abs (highDb - lastHigh) > 0.01f)
            {
                lastHigh = highDb;
                bands[0][2].setHighShelf (5000.0f, highDb, sr);
                bands[1][2].copyCoefficients (bands[0][2]);
            }
        }

        void process (float* left, float* right, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                left[i]  = bands[0][2].process (bands[0][1].process (bands[0][0].process (left[i])));
                right[i] = bands[1][2].process (bands[1][1].process (bands[1][0].process (right[i])));
            }
        }

    private:
        double sr = 44100.0;
        Biquad bands[2][3];
        float lastLow = -1000.0f, lastMid = -1000.0f, lastFreq = -1000.0f, lastHigh = -1000.0f;
    };
}
