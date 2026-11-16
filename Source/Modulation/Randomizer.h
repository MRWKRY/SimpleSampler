#pragma once

#include "ModMatrix.h"
#include "TempoSync.h"
#include "../FX/DspUtil.h"

/** Randomises the modulatable knobs.

    - randomize(): one-shot, changes the real parameter values (with undo). Message thread.
    - processAuto(): tempo-synced auto-randomise. It does NOT move the parameters; it sets
      temporary offsets in the ModMatrix that glide smoothly, so switching it off returns
      to exactly what was dialled in. Audio thread. */
class Randomizer
{
public:
    enum Scope { padScope = 0, fxScope, bothScope };
    static juce::StringArray scopeNames()           { return { "Pad", "FX", "Pad + FX" }; }

    Randomizer (ModMatrix& m, juce::AudioProcessorValueTreeState& apvts) : matrix (m)
    {
        using namespace ParamMath;
        amount   = apvts.getRawParameterValue (id::rndAmount);
        scope    = apvts.getRawParameterValue (id::rndScope);
        autoOn   = apvts.getRawParameterValue (id::rndAuto);
        interval = apvts.getRawParameterValue (id::rndInterval);
        jassert (amount && scope && autoOn && interval);
    }

    static bool inScope (const ModMatrix::Target& t, int scopeValue, int selectedPad) noexcept
    {
        if (t.pad < 0)  return scopeValue != padScope;
        return scopeValue != fxScope && t.pad == selectedPad;
    }

    // ---- message thread -----------------------------------------------------
    /** Moves every unlocked knob in scope by up to +-amount (0..1 of its range). Returns how many changed. */
    int randomize (int scopeValue, float amount01, int selectedPad)
    {
        std::vector<Saved> snapshot;
        int changed = 0;

        for (int i = 0; i < matrix.getNumTargets(); ++i)
        {
            auto& t = matrix.getTarget (i);
            if (t.locked.load() || ! inScope (t, scopeValue, selectedPad))
                continue;

            const float before = t.param->getValue();
            const float after = juce::jlimit (0.0f, 1.0f, before + (random.nextFloat() * 2.0f - 1.0f) * amount01);
            snapshot.push_back ({ i, before });

            if (std::abs (after - before) > 1.0e-6f)
            {
                t.param->beginChangeGesture();
                t.param->setValueNotifyingHost (after);
                t.param->endChangeGesture();
                ++changed;
            }
        }

        if (! snapshot.empty())
        {
            undoStack.push_back (std::move (snapshot));
            if (undoStack.size() > maxUndo)
                undoStack.erase (undoStack.begin());
        }
        return changed;
    }

    int randomize (int selectedPad)
    {
        return randomize (juce::roundToInt (scope->load()), amount->load() * 0.01f, selectedPad);
    }

    bool canUndo() const noexcept                   { return ! undoStack.empty(); }

    bool undo()
    {
        if (undoStack.empty())
            return false;

        for (const auto& s : undoStack.back())
        {
            auto& t = matrix.getTarget (s.target);
            t.param->beginChangeGesture();
            t.param->setValueNotifyingHost (s.value);
            t.param->endChangeGesture();
        }
        undoStack.pop_back();
        return true;
    }

    void clearUndo()                                { undoStack.clear(); }
    void setSeed (juce::int64 seed)                 { random.setSeed (seed); }

    // ---- audio thread -------------------------------------------------------
    void processAuto (double beatBefore, double beatAfter, int selectedPad) noexcept
    {
        const bool on = autoOn->load() > 0.5f;
        if (! on)
        {
            if (wasOn)
                matrix.clearRandomTargets();
            wasOn = false;
            return;
        }

        const double length = TempoSync::intervalBeats (juce::roundToInt (interval->load()));
        const bool crossed = std::floor (beatAfter / length) != std::floor (beatBefore / length);
        if (wasOn && ! crossed)
            return;

        wasOn = true;
        const int scopeValue = juce::roundToInt (scope->load());
        const float range = amount->load() * 0.01f;

        for (int i = 0; i < matrix.getNumTargets(); ++i)
        {
            const auto& t = matrix.getTarget (i);
            const bool use = ! t.locked.load (std::memory_order_relaxed) && inScope (t, scopeValue, selectedPad);
            matrix.setRandomTarget (i, use ? rng.nextBipolar() * range : 0.0f);
        }
    }

private:
    struct Saved { int target; float value; };
    static constexpr size_t maxUndo = 16;

    ModMatrix& matrix;
    std::atomic<float>* amount = nullptr;
    std::atomic<float>* scope = nullptr;
    std::atomic<float>* autoOn = nullptr;
    std::atomic<float>* interval = nullptr;

    juce::Random random;
    std::vector<std::vector<Saved>> undoStack;

    fx::Rng rng { 0xA5A5F00Du };
    bool wasOn = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Randomizer)
};
