#pragma once

#include "DspUtil.h"

namespace fx
{
    /** Feedback-delay-network reverb: 8 delay lines mixed through a Householder matrix,
        input diffusion all-passes, per-line damping and a little delay modulation
        (smoother and less metallic than a comb-filter reverb). Output is fully wet. */
    class FdnReverb
    {
    public:
        static constexpr int numLines = 8;

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            const int maxLine = (int) ((0.0617 * maxScale + 2.0 * modDepth) * sr) + 64;
            for (int i = 0; i < numLines; ++i)
            {
                lines[i].prepare (maxLine);
                lfoPhase[i] = (float) i / (float) numLines;
                lfoInc[i] = (0.11f + 0.037f * (float) i) / (float) sr;
            }
            static const float apMs[4] = { 4.7f, 3.6f, 12.7f, 9.3f };
            for (int i = 0; i < 4; ++i)
            {
                apLen[0][i] = juce::jmax (2, (int) (apMs[i] * 0.001f * (float) sr));
                apLen[1][i] = juce::jmax (2, (int) (apMs[i] * 0.001f * 1.07f * (float) sr));
                ap[0][i].prepare (apLen[0][i] + 4);
                ap[1][i].prepare (apLen[1][i] + 4);
            }
            setParameters (0.5f, 2.5f, 7000.0f);
            for (int i = 0; i < numLines; ++i) length[i] = targetLength[i];
            reset();
        }

        void reset() noexcept
        {
            for (auto& l : lines) l.reset();
            for (auto& ch : ap) for (auto& a : ch) a.reset();
            for (auto& z : damp) z = 0.0f;
        }

        /** size 0..1, decay = RT60 in seconds, dampingHz = high-frequency roll-off inside the tail. */
        void setParameters (float size, float decaySeconds, float dampingHz) noexcept
        {
            static const float baseMs[numLines] = { 29.7f, 37.1f, 41.1f, 43.7f, 47.3f, 53.9f, 59.3f, 61.7f };
            const float scale = 0.35f + (maxScale - 0.35f) * juce::jlimit (0.0f, 1.0f, size);
            const float rt = juce::jmax (0.1f, decaySeconds);

            for (int i = 0; i < numLines; ++i)
            {
                const float seconds = baseMs[i] * 0.001f * scale;
                targetLength[i] = seconds * (float) sr;
                feedback[i] = std::pow (10.0f, -3.0f * seconds / rt);
            }
            dampCoef = 1.0f - std::exp (-twoPi * juce::jlimit (500.0f, (float) sr * 0.45f, dampingHz) / (float) sr);
        }

        void processSample (float inL, float inR, float& outL, float& outR) noexcept
        {
            // input diffusion
            for (int i = 0; i < 4; ++i)
            {
                inL = allpass (ap[0][i], apLen[0][i], inL);
                inR = allpass (ap[1][i], apLen[1][i], inR);
            }

            float v[numLines], sum = 0.0f;
            for (int i = 0; i < numLines; ++i)
            {
                length[i] += (targetLength[i] - length[i]) * 0.0004f;
                lfoPhase[i] += lfoInc[i];
                if (lfoPhase[i] >= 1.0f) lfoPhase[i] -= 1.0f;
                const float mod = modDepth * (float) sr * (1.0f + triangle (lfoPhase[i]));

                const float d = lines[i].read (length[i] + mod);
                damp[i] += dampCoef * (d - damp[i]);
                v[i] = damp[i] * feedback[i];
                sum += v[i];
            }

            sum *= 2.0f / (float) numLines;
            outL = outR = 0.0f;
            for (int i = 0; i < numLines; ++i)
            {
                lines[i].write (v[i] - sum + ((i & 1) ? inR : inL));
                const float tap = v[i] * (((i >> 1) & 1) ? -1.0f : 1.0f);
                if (i & 1) outR += tap; else outL += tap;
            }
            outL *= 0.6f;
            outR *= 0.6f;
        }

        void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
                processSample (inL[i], inR[i], outL[i], outR[i]);
        }

    private:
        static float triangle (float p) noexcept        { return 1.0f - 4.0f * std::abs (p - 0.5f); }

        static float allpass (DelayLine& line, int len, float x) noexcept
        {
            constexpr float g = 0.62f;
            const float z = line.readInt (len - 1);
            const float y = z - g * x;
            line.write (x + g * y);
            return y;
        }

        static constexpr float maxScale = 1.9f;
        static constexpr float modDepth = 0.00035f;     // seconds

        double sr = 44100.0;
        DelayLine lines[numLines];
        DelayLine ap[2][4];
        int apLen[2][4] = {};
        float length[numLines] = {}, targetLength[numLines] = {}, feedback[numLines] = {};
        float damp[numLines] = {}, lfoPhase[numLines] = {}, lfoInc[numLines] = {};
        float dampCoef = 0.5f;
    };
}
