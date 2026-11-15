#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../ParamMath.h"
#include <atomic>
#include <memory>
#include <vector>

/** Routes the four modulators (and the auto-randomiser) to parameters.

    - A "target" is a parameter that may be modulated (pad tone / genre knobs, global FX knobs).
    - A "slot" is one assignment: source -> target with a depth of -1..+1 (= -100..+100 % of the knob's range).
    - The audio thread asks get(target) for the value to use right now (parameter value + modulation).

    Slots are a fixed table of atomics, so editing from the UI never blocks or allocates on the audio thread. */
class ModMatrix
{
public:
    static constexpr int numSources = ParamMath::numMods;
    static constexpr int maxSlots = 64;

    struct Target
    {
        juce::String paramID, name;
        juce::RangedAudioParameter* param = nullptr;
        std::atomic<float>* raw = nullptr;
        juce::NormalisableRange<float> range;
        int pad = -1;                               // -1 = global parameter
        bool lockedByDefault = false;
        std::atomic<bool>  locked { false };        // randomiser lock
        std::atomic<float> uiOffset { 0.0f };       // current modulation offset (normalised), for the knob marker
        std::atomic<int>   numAssignments { 0 };
    };

    struct SlotInfo { int slot = -1, source = 0, target = -1; float depth = 0.0f; };

    ModMatrix() = default;

    // ---- setup (constructor of the processor) -------------------------------
    int addTarget (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID, int pad, bool lockedByDefault = false);
    void finishSetup();

    int getNumTargets() const noexcept                              { return (int) targets.size(); }
    Target& getTarget (int index) noexcept                          { return *targets[(size_t) index]; }
    const Target& getTarget (int index) const noexcept              { return *targets[(size_t) index]; }
    int findTarget (const juce::String& paramID) const;

    // ---- editing (message thread) -------------------------------------------
    /** Adds an assignment, or updates the depth if this source already drives this target. Returns the slot or -1 when full. */
    int assign (int source, int target, float depth);
    void setDepth (int slot, float depth);
    void removeSlot (int slot);
    void removeAllFor (int target);
    void clear();

    std::vector<SlotInfo> getSlots() const;
    std::vector<SlotInfo> getSlotsFor (int target) const;
    /** Sum of |depth| of everything assigned to the target (for drawing the range arc). */
    float getTotalDepth (int target) const;

    void resetLocks();
    juce::ValueTree toTree() const;
    /** An invalid tree clears all assignments and restores the default locks. */
    void fromTree (const juce::ValueTree& tree);

    /** Bumped on every edit (the editor polls this to rebuild its list). */
    std::atomic<uint32_t> version { 0 };

    // ---- audio thread ---------------------------------------------------------
    void prepare (double sampleRate);
    /** Call once per processing segment with the current modulator outputs (-1..1). */
    void update (const float* sourceValues, int numSamples) noexcept;
    /** Publishes the current offsets for the UI (call once per block). */
    void publishForUi() noexcept;

    /** Parameter value with modulation applied, in the parameter's own units. */
    float get (int target) const noexcept
    {
        const auto& t = *targets[(size_t) target];
        const float raw = t.raw->load (std::memory_order_relaxed);
        const float offset = offsets[(size_t) target];
        if (offset == 0.0f)
            return raw;
        return t.range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, t.range.convertTo0to1 (raw) + offset));
    }

    /** Random offsets of the auto-randomiser (normalised units, glide towards their targets). */
    void setRandomTarget (int target, float offset) noexcept        { randomTarget[(size_t) target] = offset; }
    void clearRandomTargets() noexcept                              { std::fill (randomTarget.begin(), randomTarget.end(), 0.0f); }

    static const juce::Identifier treeType;

private:
    struct Slot
    {
        std::atomic<int> target { -1 };
        std::atomic<int> source { 0 };
        std::atomic<float> depth { 0.0f };
    };

    void recount();

    std::vector<std::unique_ptr<Target>> targets;
    juce::HashMap<juce::String, int> lookup;
    Slot slots[maxSlots];

    std::vector<float> offsets, randomTarget, randomCurrent;
    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModMatrix)
};
