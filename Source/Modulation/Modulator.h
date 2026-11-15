#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "TempoSync.h"
#include "../ParamMath.h"
#include <atomic>

/** Host tempo and position for one processing block (falls back to the manual tempo). */
struct TempoClock
{
    double bpm = 120.0;
    double beat = 0.0;          // running position in quarter notes (follows the host while it plays)
    bool hostPlaying = false, hostTempo = false;

    void beginBlock (juce::AudioPlayHead* playHead, double manualBpm) noexcept
    {
        hostPlaying = false;
        hostTempo = false;
        double newBpm = manualBpm;

        if (playHead != nullptr)
        {
            if (const auto position = playHead->getPosition())
            {
                if (const auto hostBpm = position->getBpm(); hostBpm.hasValue() && *hostBpm > 1.0)
                {
                    newBpm = *hostBpm;
                    hostTempo = true;
                }
                if (position->getIsPlaying())
                {
                    if (const auto ppq = position->getPpqPosition(); ppq.hasValue())
                    {
                        beat = *ppq;
                        hostPlaying = true;
                    }
                }
            }
        }
        bpm = juce::jlimit (20.0, 400.0, newBpm);
    }

    void advance (int numSamples, double sampleRate) noexcept
    {
        beat += (double) numSamples * bpm / (60.0 * sampleRate);
    }
};

/** One of the four modulators (LFO / random / step sequencer). Output is -1..1, one value per segment. */
class Modulator
{
public:
    void attach (juce::AudioProcessorValueTreeState& apvts, int modIndex)
    {
        using namespace ParamMath;
        seed = (juce::uint32) (modIndex + 1) * 7919u;
        type = apvts.getRawParameterValue (id::mod (modIndex, id::modType));
        sync = apvts.getRawParameterValue (id::mod (modIndex, id::modSync));
        rate = apvts.getRawParameterValue (id::mod (modIndex, id::modRate));
        hz   = apvts.getRawParameterValue (id::mod (modIndex, id::modHz));
        mode = apvts.getRawParameterValue (id::mod (modIndex, id::modMode));
        for (int i = 0; i < TempoSync::numSteps; ++i)
            steps[i] = apvts.getRawParameterValue (id::modStep (modIndex, i));
        jassert (type && sync && rate && hz && mode);
    }

    void reset() noexcept                           { units = 0.0; }
    /** Note-on: restarts the cycle when the mode is "Retrigger". */
    void noteOn() noexcept
    {
        if (juce::roundToInt (mode->load()) == TempoSync::retrigger)
            units = 0.0;
    }

    /** Returns the value at the start of the segment, then moves on by numSamples. */
    float advance (int numSamples, const TempoClock& clock, double sampleRate) noexcept
    {
        const int shape = juce::roundToInt (type->load());
        const bool synced = sync->load() > 0.5f;
        const int division = juce::roundToInt (rate->load());
        const int m = juce::roundToInt (mode->load());

        float stepValues[TempoSync::numSteps];
        for (int i = 0; i < TempoSync::numSteps; ++i)
            stepValues[i] = steps[i]->load (std::memory_order_relaxed);

        // "Host position": the phase is derived from the song position, so it always lines up with the bars
        const bool followHost = (m == TempoSync::hostPosition) && synced;
        if (followHost)
            units = TempoSync::unitsFromBeats (clock.beat, division);

        const float value = TempoSync::shapeValue (shape, units, stepValues, seed);

        if (! followHost)
            units += (double) numSamples * TempoSync::unitsPerSample (synced, division, hz->load(), clock.bpm, sampleRate);

        uiValue.store (value, std::memory_order_relaxed);
        uiUnits.store ((float) TempoSync::fraction (shape == TempoSync::stepSeq ? units / TempoSync::numSteps : units),
                       std::memory_order_relaxed);
        return value;
    }

    std::atomic<float> uiValue { 0.0f };    // last output, for the activity display
    std::atomic<float> uiUnits { 0.0f };    // 0..1 position inside the cycle (or the 16-step pattern)

private:
    std::atomic<float>* type = nullptr;
    std::atomic<float>* sync = nullptr;
    std::atomic<float>* rate = nullptr;
    std::atomic<float>* hz = nullptr;
    std::atomic<float>* mode = nullptr;
    std::atomic<float>* steps[TempoSync::numSteps] = {};
    juce::uint32 seed = 1;
    double units = 0.0;
};
