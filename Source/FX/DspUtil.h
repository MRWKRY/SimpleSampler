#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

// Small building blocks shared by the effects. Everything is allocation-free after prepare().
namespace fx
{
    constexpr float pi = juce::MathConstants<float>::pi;
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    inline float dbToGain (float db, float silentBelow = -59.9f) noexcept
    {
        return db <= silentBelow ? 0.0f : std::pow (10.0f, db * 0.05f);
    }

    /** One-pole smoothing coefficient for `numSamples` at a time constant in seconds. */
    inline float smoothCoef (int numSamples, float seconds, double sampleRate) noexcept
    {
        return 1.0f - std::exp (-(float) numSamples / (juce::jmax (1.0e-4f, seconds) * (float) sampleRate));
    }

    /** A value that moves towards its target once per segment and is linearly interpolated inside the segment. */
    struct Ramp
    {
        float current = 0.0f, step = 0.0f;

        void snap (float v) noexcept                    { current = v; step = 0.0f; }
        void setTarget (float target, int numSamples, float coef) noexcept
        {
            if (std::abs (target - current) < 2.0e-5f) { current = target; step = 0.0f; return; }
            float next = current + (target - current) * coef;
            if (std::abs (target - next) < 1.0e-4f) next = target;      // land exactly, so "0" really means off
            step = numSamples > 0 ? (next - current) / (float) numSamples : 0.0f;
        }
        float next() noexcept                           { current += step; return current; }
        /** Segment-rate use: jump to the end-of-segment value. */
        float skip (int numSamples) noexcept            { current += step * (float) numSamples; return current; }
    };

    struct OnePole
    {
        float a = 1.0f, z = 0.0f;
        void setCutoff (float hz, double sampleRate) noexcept
        {
            a = 1.0f - std::exp (-twoPi * juce::jlimit (5.0f, (float) sampleRate * 0.49f, hz) / (float) sampleRate);
        }
        float lowpass (float x) noexcept                { z += a * (x - z); return z; }
        float highpass (float x) noexcept               { z += a * (x - z); return x - z; }
        void reset() noexcept                           { z = 0.0f; }
    };

    /** RBJ biquad (transposed direct form II). */
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() noexcept                           { z1 = z2 = 0.0f; }
        void copyCoefficients (const Biquad& o) noexcept { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }

        void setPeak (float hz, float q, float gainDb, double sr) noexcept
        {
            const float A = std::pow (10.0f, gainDb / 40.0f);
            const float w = twoPi * juce::jlimit (10.0f, (float) sr * 0.45f, hz) / (float) sr;
            const float alpha = std::sin (w) / (2.0f * q), c = std::cos (w);
            const float a0 = 1.0f + alpha / A;
            b0 = (1.0f + alpha * A) / a0; b1 = (-2.0f * c) / a0; b2 = (1.0f - alpha * A) / a0;
            a1 = (-2.0f * c) / a0;        a2 = (1.0f - alpha / A) / a0;
        }
        void setLowShelf (float hz, float gainDb, double sr) noexcept
        {
            const float A = std::pow (10.0f, gainDb / 40.0f);
            const float w = twoPi * juce::jlimit (10.0f, (float) sr * 0.45f, hz) / (float) sr;
            const float c = std::cos (w), s = std::sin (w);
            const float alpha = s * 0.5f * std::sqrt (2.0f), beta = 2.0f * std::sqrt (A) * alpha;
            const float a0 = (A + 1.0f) + (A - 1.0f) * c + beta;
            b0 = A * ((A + 1.0f) - (A - 1.0f) * c + beta) / a0;
            b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * c) / a0;
            b2 = A * ((A + 1.0f) - (A - 1.0f) * c - beta) / a0;
            a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * c) / a0;
            a2 = ((A + 1.0f) + (A - 1.0f) * c - beta) / a0;
        }
        void setHighShelf (float hz, float gainDb, double sr) noexcept
        {
            const float A = std::pow (10.0f, gainDb / 40.0f);
            const float w = twoPi * juce::jlimit (10.0f, (float) sr * 0.45f, hz) / (float) sr;
            const float c = std::cos (w), s = std::sin (w);
            const float alpha = s * 0.5f * std::sqrt (2.0f), beta = 2.0f * std::sqrt (A) * alpha;
            const float a0 = (A + 1.0f) - (A - 1.0f) * c + beta;
            b0 = A * ((A + 1.0f) + (A - 1.0f) * c + beta) / a0;
            b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * c) / a0;
            b2 = A * ((A + 1.0f) + (A - 1.0f) * c - beta) / a0;
            a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * c) / a0;
            a2 = ((A + 1.0f) - (A - 1.0f) * c - beta) / a0;
        }
    };

    /** State-variable low-pass (TPT form): stable under fast cutoff changes. */
    struct SvfLowpass
    {
        float g = 0.5f, k = 1.4f, s1 = 0.0f, s2 = 0.0f;
        void set (float hz, float q, double sr) noexcept
        {
            g = std::tan (pi * juce::jlimit (20.0f, (float) sr * 0.45f, hz) / (float) sr);
            k = 1.0f / juce::jmax (0.3f, q);
        }
        float process (float x) noexcept
        {
            const float h = (x - (k + g) * s1 - s2) / (1.0f + g * (k + g));
            const float b = g * h + s1;  s1 = g * h + b;
            const float l = g * b + s2;  s2 = g * b + l;
            return l;
        }
        void reset() noexcept                           { s1 = s2 = 0.0f; }
    };

    /** Circular delay buffer with linear-interpolated reads. */
    class DelayLine
    {
    public:
        void prepare (int maxSamples)
        {
            int size = 16;
            while (size < maxSamples + 4) size <<= 1;
            buffer.assign ((size_t) size, 0.0f);
            mask = size - 1;
            writePos = 0;
        }
        void reset() noexcept                           { std::fill (buffer.begin(), buffer.end(), 0.0f); }
        int capacity() const noexcept                   { return mask - 2; }

        void write (float x) noexcept
        {
            writePos = (writePos + 1) & mask;
            buffer[(size_t) writePos] = x;
        }
        /** delay 0 = the sample just written. */
        float read (float delaySamples) const noexcept
        {
            delaySamples = juce::jlimit (0.0f, (float) (mask - 2), delaySamples);
            const int di = (int) delaySamples;
            const float f = delaySamples - (float) di;
            const float a = buffer[(size_t) ((writePos - di) & mask)];
            const float b = buffer[(size_t) ((writePos - di - 1) & mask)];
            return a + (b - a) * f;
        }
        float readInt (int delaySamples) const noexcept { return buffer[(size_t) ((writePos - delaySamples) & mask)]; }

    private:
        std::vector<float> buffer { std::vector<float> (16, 0.0f) };
        int mask = 15, writePos = 0;
    };

    /** Tiny fast random generator for the audio thread. */
    struct Rng
    {
        juce::uint32 state = 0x12345678u;
        explicit Rng (juce::uint32 seed = 0x12345678u) : state (seed ? seed : 1u) {}
        juce::uint32 nextInt() noexcept                 { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
        float next01() noexcept                         { return (float) (nextInt() >> 8) * (1.0f / 16777216.0f); }
        float nextBipolar() noexcept                    { return next01() * 2.0f - 1.0f; }
    };

    /** Slow smooth random signal in -1..1 (interpolates between random points). */
    struct SmoothNoise
    {
        Rng rng;
        float a = 0.0f, b = 0.0f, phase = 1.0f;
        explicit SmoothNoise (juce::uint32 seed = 99u) : rng (seed) { b = rng.nextBipolar(); }

        float advance (float cycles) noexcept
        {
            phase += cycles;
            while (phase >= 1.0f) { phase -= 1.0f; a = b; b = rng.nextBipolar(); }
            const float t = phase * phase * (3.0f - 2.0f * phase);
            return a + (b - a) * t;
        }
    };

    inline bool isBad (float x) noexcept                { return ! (std::abs (x) < 64.0f); }   // also true for NaN / inf
}
