#pragma once

#include "DspUtil.h"

// "Future Bass" knobs that work on the pad's audio. Pump and Gate are plain gain curves
// (see TempoSync.h); Dive and Formant live inside the voices (see Voice.cpp).
namespace fx
{
    //==========================================================================
    /** Wobble: tempo-synced resonant low-pass. The caller supplies the LFO position (0..1 inside one cycle). */
    class Wobble
    {
    public:
        void prepare (double sampleRate)                { sr = sampleRate; reset(); }
        void reset() noexcept                           { filters[0].reset(); filters[1].reset(); }

        void process (float* left, float* right, int n, Ramp& amount, double lfoPhase) noexcept
        {
            const float a = juce::jlimit (0.0f, 1.0f, amount.current + amount.step * (float) n * 0.5f);
            // open on the beat, closed half-way through the cycle
            const float lfo = 0.5f + 0.5f * std::cos (twoPi * (float) lfoPhase);
            const float top = juce::jmin (18000.0f, (float) sr * 0.42f);
            const float low = top * std::pow (150.0f / top, a);
            const float cutoff = low * std::pow (top / low, lfo);

            filters[0].set (cutoff, 0.707f + 4.2f * a, sr);
            filters[1].g = filters[0].g;
            filters[1].k = filters[0].k;

            for (int i = 0; i < n; ++i)
            {
                const float mix = juce::jmin (1.0f, amount.next() * 10.0f);
                const float l = left[i], r = right[i];
                left[i]  = l + mix * (filters[0].process (l) - l);
                right[i] = r + mix * (filters[1].process (r) - r);
            }
        }

    private:
        double sr = 44100.0;
        SvfLowpass filters[2];
    };

    //==========================================================================
    /** Width: cross-fed, slowly detuned short delays (Haas + chorus) and a wider side signal. */
    class Width
    {
    public:
        void prepare (double sampleRate)
        {
            sr = sampleRate;
            delays[0].prepare ((int) (0.03 * sr) + 8);
            delays[1].prepare ((int) (0.03 * sr) + 8);
            reset();
        }
        void reset() noexcept                           { delays[0].reset(); delays[1].reset(); }

        void process (float* left, float* right, int n, Ramp& amount) noexcept
        {
            const float inc1 = 0.31f / (float) sr, inc2 = 0.43f / (float) sr;
            for (int i = 0; i < n; ++i)
            {
                const float a = amount.next();
                lfo1 += inc1; if (lfo1 >= 1.0f) lfo1 -= 1.0f;
                lfo2 += inc2; if (lfo2 >= 1.0f) lfo2 -= 1.0f;

                const float l = left[i], r = right[i];
                delays[0].write (l);
                delays[1].write (r);
                const float tapL = delays[0].read ((float) sr * (0.0075f + 0.0018f * std::sin (twoPi * lfo1)));
                const float tapR = delays[1].read ((float) sr * (0.0115f + 0.0018f * std::sin (twoPi * lfo2)));

                // widen the side signal, then cross-feed the detuned copies
                const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * (1.0f + 0.8f * a);
                const float norm = 1.0f / (1.0f + 0.3f * a);
                left[i]  = (mid + side + a * 0.55f * tapR) * norm;
                right[i] = (mid - side + a * 0.55f * tapL) * norm;
            }
        }

    private:
        double sr = 44100.0;
        DelayLine delays[2];
        float lfo1 = 0.0f, lfo2 = 0.5f;
    };

    //==========================================================================
    /** Crush: bit-depth and sample-rate reduction. */
    class Crush
    {
    public:
        void reset() noexcept                           { counter = 1.0f; holdL = holdR = 0.0f; }

        void process (float* left, float* right, int n, Ramp& amount) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                const float a = amount.next();
                const float levels = std::pow (2.0f, 15.0f - 12.0f * a);      // 16 bit -> 4 bit
                const float divide = 1.0f + 39.0f * a * a;                     // hold up to 40 samples

                counter += 1.0f / divide;
                if (counter >= 1.0f)
                {
                    counter -= 1.0f;
                    holdL = std::round (left[i] * levels) / levels;
                    holdR = std::round (right[i] * levels) / levels;
                }
                left[i] = holdL;
                right[i] = holdR;
            }
        }

    private:
        float counter = 1.0f, holdL = 0.0f, holdR = 0.0f;
    };
}
