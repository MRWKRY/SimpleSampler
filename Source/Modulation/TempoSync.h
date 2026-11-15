#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

// Pure tempo / modulation maths (no state) so it can be unit-tested.
namespace TempoSync
{
    // ---- note divisions (lengths in quarter-note beats, 4/4 assumed) --------
    struct Division { const char* name; double beats; };

    inline const Division* divisions() noexcept
    {
        static const Division table[] =
        {
            { "4 bars", 16.0 }, { "2 bars", 8.0 }, { "1 bar", 4.0 },
            { "1/2", 2.0 },     { "1/2.", 3.0 },     { "1/2T", 4.0 / 3.0 },
            { "1/4", 1.0 },     { "1/4.", 1.5 },     { "1/4T", 2.0 / 3.0 },
            { "1/8", 0.5 },     { "1/8.", 0.75 },    { "1/8T", 1.0 / 3.0 },
            { "1/16", 0.25 },   { "1/16.", 0.375 },  { "1/16T", 1.0 / 6.0 },
            { "1/32", 0.125 },  { "1/32.", 0.1875 }, { "1/32T", 1.0 / 12.0 }
        };
        return table;
    }

    constexpr int numDivisions = 18;
    constexpr int divBar = 2, divHalf = 3, divQuarter = 6, divEighth = 9, divEighthDotted = 10, divSixteenth = 12;

    inline double divisionBeats (int index) noexcept
    {
        return divisions()[juce::jlimit (0, numDivisions - 1, index)].beats;
    }

    inline juce::StringArray divisionNames()
    {
        juce::StringArray names;
        for (int i = 0; i < numDivisions; ++i)
            names.add (divisions()[i].name);
        return names;
    }

    inline double beatsToSeconds (double beats, double bpm) noexcept
    {
        return beats * 60.0 / juce::jmax (1.0, bpm);
    }

    /** Delay time in milliseconds for a note division at a tempo. */
    inline double divisionMs (int index, double bpm) noexcept
    {
        return beatsToSeconds (divisionBeats (index), bpm) * 1000.0;
    }

    // ---- auto-randomise intervals -------------------------------------------
    constexpr int numIntervals = 6;
    inline double intervalBeats (int index) noexcept
    {
        static const double beats[numIntervals] = { 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };
        return beats[juce::jlimit (0, numIntervals - 1, index)];
    }
    inline juce::StringArray intervalNames() { return { "1 beat", "2 beats", "1 bar", "2 bars", "4 bars", "8 bars" }; }

    // ---- modulator shapes ---------------------------------------------------
    enum Shape { sine = 0, triangle, saw, square, sampleHold, smoothRandom, stepSeq, numShapes };
    inline juce::StringArray shapeNames() { return { "Sine", "Triangle", "Saw", "Square", "Random S&H", "Random Smooth", "Step" }; }

    enum Mode { freeRun = 0, hostPosition, retrigger, numModes };
    inline juce::StringArray modeNames() { return { "Free", "Host position", "Retrigger" }; }

    constexpr int numSteps = 16;

    inline double fraction (double x) noexcept { return x - std::floor (x); }

    /** Deterministic random value in -1..1 for a cycle number (same position -> same value). */
    inline float hashToBipolar (juce::int64 cycle, juce::uint32 seed) noexcept
    {
        auto x = (juce::uint64) cycle * 0x9E3779B97F4A7C15ull + (juce::uint64) seed * 0xBF58476D1CE4E5B9ull + 0x94D049BB133111EBull;
        x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
        x ^= x >> 27; x *= 0x94D049BB133111EBull;
        x ^= x >> 31;
        return (float) ((double) (x >> 11) / (double) (1ull << 53)) * 2.0f - 1.0f;
    }

    /** Value (-1..1) of a modulator at an absolute position measured in "rate units".
        For the LFO shapes one unit is one cycle; for random and step shapes one unit is one step. */
    inline float shapeValue (int shape, double units, const float* steps, juce::uint32 seed) noexcept
    {
        const double ph = fraction (units);
        const auto cycle = (juce::int64) std::floor (units);

        switch (shape)
        {
            case sine:      return (float) std::sin (juce::MathConstants<double>::twoPi * ph);
            case triangle:  return (float) (ph < 0.25 ? 4.0 * ph : (ph < 0.75 ? 2.0 - 4.0 * ph : 4.0 * ph - 4.0));
            case saw:       return (float) (1.0 - 2.0 * ph);
            case square:    return ph < 0.5 ? 1.0f : -1.0f;
            case sampleHold: return hashToBipolar (cycle, seed);
            case smoothRandom:
            {
                const float a = hashToBipolar (cycle, seed), b = hashToBipolar (cycle + 1, seed);
                const auto t = (float) (ph * ph * (3.0 - 2.0 * ph));
                return a + (b - a) * t;
            }
            case stepSeq:
            {
                if (steps == nullptr) return 0.0f;
                const int index = (int) (((cycle % numSteps) + numSteps) % numSteps);
                return juce::jlimit (-1.0f, 1.0f, steps[index]);
            }
            default: break;
        }
        return 0.0f;
    }

    /** Position in rate units for a host position (in beats). */
    inline double unitsFromBeats (double beatPosition, int divisionIndex) noexcept
    {
        return beatPosition / divisionBeats (divisionIndex);
    }

    /** How many rate units pass per sample. */
    inline double unitsPerSample (bool synced, int divisionIndex, double hz, double bpm, double sampleRate) noexcept
    {
        if (sampleRate <= 0.0) return 0.0;
        if (synced) return (juce::jmax (1.0, bpm) / 60.0) / divisionBeats (divisionIndex) / sampleRate;
        return hz / sampleRate;
    }

    // ---- sidechain-style pump and trance gate -------------------------------
    /** Pump gain (1 = untouched). phase 0..1 inside one pump cycle, release 0..1 = part of the cycle used to recover. */
    inline float pumpGain (double phase, float amount, float release) noexcept
    {
        const double rel = juce::jlimit (0.02, 1.0, (double) release);
        if (phase >= rel) return 1.0f;
        const double e = 1.0 - phase / rel;
        return 1.0f - amount * (float) (e * e);
    }

    inline int gateStepIndex (double beatPosition, double stepBeats) noexcept
    {
        const auto k = (juce::int64) std::floor (beatPosition / stepBeats);
        return (int) (((k % numSteps) + numSteps) % numSteps);
    }
}
