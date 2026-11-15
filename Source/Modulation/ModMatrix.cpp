#include "ModMatrix.h"

const juce::Identifier ModMatrix::treeType { "Modulation" };

namespace
{
    const juce::Identifier slotType { "Slot" }, lockType { "Lock" };
    const juce::Identifier sourceProp { "source" }, paramProp { "param" }, depthProp { "depth" }, lockedProp { "locked" };
}

int ModMatrix::addTarget (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID, int pad, bool lockedByDefault)
{
    auto* param = apvts.getParameter (paramID);
    auto* raw = apvts.getRawParameterValue (paramID);
    jassert (param != nullptr && raw != nullptr);
    if (param == nullptr || raw == nullptr)
        return -1;

    auto t = std::make_unique<Target>();
    t->paramID = paramID;
    t->name = param->getName (64);
    t->param = param;
    t->raw = raw;
    t->range = param->getNormalisableRange();
    t->pad = pad;
    t->lockedByDefault = lockedByDefault;
    t->locked.store (lockedByDefault);

    const int index = (int) targets.size();
    lookup.set (paramID, index);
    targets.push_back (std::move (t));
    return index;
}

void ModMatrix::finishSetup()
{
    offsets.assign (targets.size(), 0.0f);
    randomTarget.assign (targets.size(), 0.0f);
    randomCurrent.assign (targets.size(), 0.0f);
}

int ModMatrix::findTarget (const juce::String& paramID) const
{
    return lookup.contains (paramID) ? lookup[paramID] : -1;
}

//==============================================================================
int ModMatrix::assign (int source, int target, float depth)
{
    if (target < 0 || target >= getNumTargets() || source < 0 || source >= numSources)
        return -1;

    depth = juce::jlimit (-1.0f, 1.0f, depth);

    int freeSlot = -1;
    for (int i = 0; i < maxSlots; ++i)
    {
        const int t = slots[i].target.load();
        if (t == target && slots[i].source.load() == source)
        {
            slots[i].depth.store (depth);
            ++version;
            return i;
        }
        if (t < 0 && freeSlot < 0)
            freeSlot = i;
    }

    if (freeSlot < 0)
        return -1;

    slots[freeSlot].source.store (source);
    slots[freeSlot].depth.store (depth);
    slots[freeSlot].target.store (target);   // last: this activates the slot
    recount();
    ++version;
    return freeSlot;
}

void ModMatrix::setDepth (int slot, float depth)
{
    if (slot >= 0 && slot < maxSlots && slots[slot].target.load() >= 0)
    {
        slots[slot].depth.store (juce::jlimit (-1.0f, 1.0f, depth));
        ++version;
    }
}

void ModMatrix::removeSlot (int slot)
{
    if (slot >= 0 && slot < maxSlots)
    {
        slots[slot].target.store (-1);
        recount();
        ++version;
    }
}

void ModMatrix::removeAllFor (int target)
{
    for (auto& s : slots)
        if (s.target.load() == target)
            s.target.store (-1);
    recount();
    ++version;
}

void ModMatrix::clear()
{
    for (auto& s : slots)
        s.target.store (-1);
    recount();
    ++version;
}

void ModMatrix::recount()
{
    for (auto& t : targets)
        t->numAssignments.store (0);
    for (auto& s : slots)
    {
        const int t = s.target.load();
        if (t >= 0 && t < getNumTargets())
            ++targets[(size_t) t]->numAssignments;
    }
}

std::vector<ModMatrix::SlotInfo> ModMatrix::getSlots() const
{
    std::vector<SlotInfo> result;
    for (int i = 0; i < maxSlots; ++i)
    {
        const int t = slots[i].target.load();
        if (t >= 0)
            result.push_back ({ i, slots[i].source.load(), t, slots[i].depth.load() });
    }
    return result;
}

std::vector<ModMatrix::SlotInfo> ModMatrix::getSlotsFor (int target) const
{
    std::vector<SlotInfo> result;
    for (int i = 0; i < maxSlots; ++i)
        if (slots[i].target.load() == target)
            result.push_back ({ i, slots[i].source.load(), target, slots[i].depth.load() });
    return result;
}

float ModMatrix::getTotalDepth (int target) const
{
    float total = 0.0f;
    for (auto& s : slots)
        if (s.target.load() == target)
            total += std::abs (s.depth.load());
    return total;
}

//==============================================================================
void ModMatrix::resetLocks()
{
    for (auto& t : targets)
        t->locked.store (t->lockedByDefault);
    ++version;
}

juce::ValueTree ModMatrix::toTree() const
{
    juce::ValueTree tree (treeType);
    for (const auto& s : getSlots())
    {
        juce::ValueTree v (slotType);
        v.setProperty (sourceProp, s.source, nullptr);
        v.setProperty (paramProp, targets[(size_t) s.target]->paramID, nullptr);
        v.setProperty (depthProp, s.depth, nullptr);
        tree.appendChild (v, nullptr);
    }

    // only locks that differ from the default are stored
    for (const auto& t : targets)
    {
        if (t->locked.load() != t->lockedByDefault)
        {
            juce::ValueTree v (lockType);
            v.setProperty (paramProp, t->paramID, nullptr);
            v.setProperty (lockedProp, t->locked.load(), nullptr);
            tree.appendChild (v, nullptr);
        }
    }
    return tree;
}

void ModMatrix::fromTree (const juce::ValueTree& tree)
{
    for (auto& s : slots)
        s.target.store (-1);
    for (auto& t : targets)
        t->locked.store (t->lockedByDefault);

    if (tree.isValid())
    {
        int next = 0;
        for (const auto& v : tree)
        {
            const int target = findTarget (v.getProperty (paramProp).toString());
            if (target < 0)
                continue;       // unknown parameter (e.g. written by a newer version): ignore

            if (v.hasType (slotType) && next < maxSlots)
            {
                const int source = (int) v.getProperty (sourceProp, 0);
                if (source < 0 || source >= numSources)
                    continue;
                slots[next].source.store (source);
                slots[next].depth.store (juce::jlimit (-1.0f, 1.0f, (float) v.getProperty (depthProp, 0.0f)));
                slots[next].target.store (target);
                ++next;
            }
            else if (v.hasType (lockType))
            {
                targets[(size_t) target]->locked.store ((bool) v.getProperty (lockedProp, false));
            }
        }
    }

    recount();
    ++version;
}

//==============================================================================
void ModMatrix::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    std::fill (offsets.begin(), offsets.end(), 0.0f);
    std::fill (randomCurrent.begin(), randomCurrent.end(), 0.0f);
}

void ModMatrix::update (const float* sourceValues, int numSamples) noexcept
{
    // random offsets glide to their targets (~60 ms) so auto-randomise never jumps
    const float coef = 1.0f - std::exp (-(float) numSamples / (0.06f * (float) sampleRate));
    const size_t n = offsets.size();
    for (size_t i = 0; i < n; ++i)
    {
        float r = randomCurrent[i] + (randomTarget[i] - randomCurrent[i]) * coef;
        if (std::abs (r) < 1.0e-5f && randomTarget[i] == 0.0f)
            r = 0.0f;
        randomCurrent[i] = r;
        offsets[i] = r;
    }

    for (auto& s : slots)
    {
        const int t = s.target.load (std::memory_order_relaxed);
        if (t < 0 || (size_t) t >= n)
            continue;
        const int src = s.source.load (std::memory_order_relaxed);
        if (src >= 0 && src < numSources)
            offsets[(size_t) t] += s.depth.load (std::memory_order_relaxed) * sourceValues[src];
    }
}

void ModMatrix::publishForUi() noexcept
{
    const size_t n = offsets.size();
    for (size_t i = 0; i < n; ++i)
        targets[i]->uiOffset.store (offsets[i], std::memory_order_relaxed);
}
