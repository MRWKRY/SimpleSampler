#include "Knob.h"
#include "../PastelLookAndFeel.h"

namespace
{
    constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
    constexpr float endAngle   = juce::MathConstants<float>::pi * 2.75f;

    float angleFor (float normalised)
    {
        return startAngle + juce::jlimit (0.0f, 1.0f, normalised) * (endAngle - startAngle);
    }

    void strokeArc (juce::Graphics& g, juce::Point<float> centre, float radius, float from, float to, float thickness, juce::Colour colour)
    {
        if (std::abs (to - from) < 1.0e-4f)
            return;
        juce::Path p;
        p.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void layoutKnobRow (juce::Rectangle<int> row, std::initializer_list<juce::Component*> items)
{
    const int n = (int) items.size();
    if (n == 0)
        return;

    const float w = (float) row.getWidth() / (float) n;
    int i = 0;
    for (auto* c : items)
    {
        c->setBounds (juce::Rectangle<float> ((float) row.getX() + w * (float) i, (float) row.getY(), w, (float) row.getHeight()).toNearestInt());
        ++i;
    }
}

//==============================================================================
Knob::Knob (SimpleSamplerProcessor& p, const juce::String& labelText) : processor (p), label (labelText)
{
    setRepaintsOnMouseActivity (false);
    startTimerHz (30);
}

Knob::~Knob()
{
    stopTimer();
}

void Knob::setParameter (const juce::String& paramID)
{
    attachment.reset();
    param = paramID.isNotEmpty() ? processor.apvts.getParameter (paramID) : nullptr;
    target = paramID.isNotEmpty() ? processor.getModMatrix().findTarget (paramID) : -1;
    matrixVersion = 0xffffffffu;
    totalDepth = liveOffset = 0.0f;
    firstSource = -1;
    locked = false;

    if (param != nullptr)
    {
        const auto& range = param->getNormalisableRange();
        const float def = param->getDefaultValue();
        bipolar = range.start < 0.0f && range.end > 0.0f && std::abs (def - param->convertTo0to1 (0.0f)) < 0.02f;

        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v)
        {
            value = param->convertTo0to1 (v);
            repaint();
        });
        attachment->sendInitialUpdate();
    }
    repaint();
}

void Knob::timerCallback()
{
    if (target < 0 || ! isShowing())
        return;

    auto& matrix = processor.getModMatrix();
    const auto version = matrix.version.load();
    bool changed = false;

    if (version != matrixVersion)
    {
        matrixVersion = version;
        const auto slots = matrix.getSlotsFor (target);
        totalDepth = 0.0f;
        firstSource = slots.empty() ? -1 : slots.front().source;
        for (const auto& s : slots)
            totalDepth += std::abs (s.depth);
        locked = matrix.getTarget (target).locked.load();
        changed = true;
    }

    const float offset = matrix.getTarget (target).uiOffset.load (std::memory_order_relaxed);
    if (std::abs (offset - liveOffset) > 0.002f || (offset == 0.0f && liveOffset != 0.0f))
    {
        liveOffset = offset;
        changed = true;
    }

    if (changed)
        repaint();
}

//==============================================================================
void Knob::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    auto textArea = area.removeFromBottom (15);
    const auto knobArea = area.toFloat().reduced (2.0f);
    const float size = juce::jmin (knobArea.getWidth(), knobArea.getHeight());
    const auto centre = knobArea.getCentre();
    const bool active = isEnabled() && param != nullptr;

    const float outer = size * 0.5f - 1.5f;              // modulation ring
    const float arcRadius = outer - 4.0f;                 // value arc
    const float thickness = juce::jlimit (2.2f, 3.2f, size * 0.07f);

    // ---- value arc
    strokeArc (g, centre, arcRadius, startAngle, endAngle, thickness, PastelLookAndFeel::track());
    const auto valueColour = active ? PastelLookAndFeel::accent() : PastelLookAndFeel::textSoft().withAlpha (0.6f);
    const float from = bipolar && param != nullptr ? angleFor (param->convertTo0to1 (0.0f)) : startAngle;
    strokeArc (g, centre, arcRadius, from, angleFor (value), thickness, valueColour);

    // ---- modulation: range arc + live marker on the outer ring
    const bool modulated = target >= 0 && (totalDepth > 0.0f || std::abs (liveOffset) > 0.0005f);
    if (modulated)
    {
        const auto colour = firstSource >= 0 ? PastelLookAndFeel::modColour (firstSource) : PastelLookAndFeel::textSoft();
        if (totalDepth > 0.0f)
            strokeArc (g, centre, outer, angleFor (value - totalDepth), angleFor (value + totalDepth), 1.8f, colour.withAlpha (0.45f));

        const float a = angleFor (value + liveOffset);
        const juce::Point<float> dot (centre.x + outer * std::sin (a), centre.y - outer * std::cos (a));
        g.setColour (colour);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (dot));
    }

    // ---- body + pointer
    const float body = arcRadius - thickness - 2.0f;
    if (body > 3.0f)
    {
        g.setColour (juce::Colour (0xff6b5ca5).withAlpha (0.10f));
        g.fillEllipse (juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (centre.translated (0.0f, 1.2f)));
        g.setColour (juce::Colours::white.withAlpha (active ? 1.0f : 0.7f));
        g.fillEllipse (juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (centre));

        const float a = angleFor (value);
        const float dotRadius = juce::jmax (1.4f, body * 0.15f);
        const float distance = body - dotRadius * 2.3f;
        g.setColour (valueColour);
        g.fillEllipse (juce::Rectangle<float> (dotRadius * 2.0f, dotRadius * 2.0f)
                           .withCentre ({ centre.x + distance * std::sin (a), centre.y - distance * std::cos (a) }));
    }

    if (dropHover)
    {
        g.setColour (PastelLookAndFeel::accent().withAlpha (0.18f));
        g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (centre));
        g.setColour (PastelLookAndFeel::accent());
        g.drawEllipse (juce::Rectangle<float> (size - 2.0f, size - 2.0f).withCentre (centre), 1.6f);
    }

    // ---- randomiser lock: a small padlock at the top right
    if (locked)
    {
        const juce::Rectangle<float> lock (knobArea.getRight() - 9.0f, knobArea.getY() + 4.0f, 7.0f, 5.5f);
        g.setColour (PastelLookAndFeel::textSoft());
        g.fillRoundedRectangle (lock, 1.3f);
        juce::Path shackle;
        shackle.addCentredArc (lock.getCentreX(), lock.getY(), 2.2f, 2.6f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        g.strokePath (shackle, juce::PathStrokeType (1.2f));
    }

    // ---- name, or the value while the mouse is over the knob
    const bool showValue = (hover || dragging) && active;
    g.setFont (PastelLookAndFeel::font (11.0f, showValue));
    g.setColour (showValue ? PastelLookAndFeel::text() : PastelLookAndFeel::textSoft().withAlpha (active ? 1.0f : 0.6f));
    g.drawFittedText (showValue ? param->getCurrentValueAsText() : label, textArea.expanded (6, 0), juce::Justification::centred, 1, 0.75f);
}

//==============================================================================
void Knob::setNormalised (float newValue)
{
    if (param != nullptr && attachment != nullptr)
        attachment->setValueAsPartOfGesture (param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, newValue)));
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (param == nullptr || ! isEnabled())
        return;

    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }

    dragging = true;
    dragValue = value;
    lastDrag = e.position;
    attachment->beginGesture();
    repaint();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    // up / right = more; Shift = ten times finer
    const float delta = (e.position.x - lastDrag.x) - (e.position.y - lastDrag.y);
    lastDrag = e.position;
    dragValue = juce::jlimit (0.0f, 1.0f, dragValue + delta / 170.0f * (e.mods.isShiftDown() ? 0.1f : 1.0f));
    setNormalised (dragValue);
}

void Knob::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        dragging = false;
        attachment->endGesture();
        repaint();
    }
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (param == nullptr || ! isEnabled() || e.mods.isPopupMenu())
        return;

    attachment->setValueAsCompleteGesture (param->convertFrom0to1 (param->getDefaultValue()));
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (param == nullptr || ! isEnabled() || std::abs (wheel.deltaY) < 1.0e-4f)
    {
        Component::mouseWheelMove (e, wheel);
        return;
    }

    const float direction = (wheel.deltaY > 0 ? 1.0f : -1.0f) * (wheel.isReversed ? -1.0f : 1.0f);
    const float next = juce::jlimit (0.0f, 1.0f, value + direction * (e.mods.isShiftDown() ? 0.004f : 0.02f));
    attachment->setValueAsCompleteGesture (param->convertFrom0to1 (next));
}

//==============================================================================
bool Knob::isInterestedInDragSource (const SourceDetails& details)
{
    return target >= 0 && isEnabled() && details.description.toString().startsWith (dragPrefix());
}

void Knob::itemDropped (const SourceDetails& details)
{
    dropHover = false;
    const int source = details.description.toString().fromFirstOccurrenceOf (dragPrefix(), false, false).getIntValue();
    if (target >= 0 && source >= 0 && source < ModMatrix::numSources)
        if (processor.getModMatrix().assign (source, target, 0.5f) < 0)
            juce::Logger::writeToLog ("SimpleSampler: modulation table is full");
    repaint();
}

void Knob::showMenu()
{
    enum { resetId = 1, lockId, removeAllId, firstSlotId = 100 };
    static const int depths[] = { 100, 75, 50, 25, 10, -10, -25, -50, -75, -100 };
    static constexpr int perSlot = 16;

    auto& matrix = processor.getModMatrix();
    const auto slots = target >= 0 ? matrix.getSlotsFor (target) : std::vector<ModMatrix::SlotInfo>();

    juce::PopupMenu menu;
    menu.addSectionHeader (param->getName (48));
    menu.addItem (resetId, "Reset to default");

    if (target >= 0)
    {
        menu.addItem (lockId, "Lock (keep when randomizing)", true, matrix.getTarget (target).locked.load());

        if (! slots.empty())
        {
            menu.addSeparator();
            for (size_t i = 0; i < slots.size(); ++i)
            {
                juce::PopupMenu sub;
                for (int d = 0; d < (int) std::size (depths); ++d)
                    sub.addItem (firstSlotId + (int) i * perSlot + d, "Depth " + ParamMath::signedText (depths[d]) + " %", true,
                                 juce::roundToInt (slots[i].depth * 100.0f) == depths[d]);
                sub.addSeparator();
                sub.addItem (firstSlotId + (int) i * perSlot + perSlot - 1, "Remove");
                menu.addSubMenu ("Mod " + juce::String (slots[i].source + 1) + "   " + ParamMath::signedText (slots[i].depth * 100.0f) + " %", sub);
            }
            menu.addItem (removeAllId, "Remove all modulation");
        }
        else
        {
            menu.addSeparator();
            menu.addItem (999, "Drag a MOD chip here to modulate", false);
        }
    }

    juce::Component::SafePointer<Knob> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, slots] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        auto& self = *safe;
        auto& m = self.processor.getModMatrix();

        if (result == resetId)
        {
            self.attachment->setValueAsCompleteGesture (self.param->convertFrom0to1 (self.param->getDefaultValue()));
        }
        else if (result == lockId && self.target >= 0)
        {
            auto& t = m.getTarget (self.target);
            t.locked.store (! t.locked.load());
            ++m.version;
        }
        else if (result == removeAllId && self.target >= 0)
        {
            m.removeAllFor (self.target);
        }
        else if (result >= firstSlotId)
        {
            const int index = (result - firstSlotId) / perSlot, item = (result - firstSlotId) % perSlot;
            if (index < (int) slots.size())
            {
                if (item == perSlot - 1)                         m.removeSlot (slots[(size_t) index].slot);
                else if (item < (int) std::size (depths))        m.setDepth (slots[(size_t) index].slot, (float) depths[item] * 0.01f);
            }
        }
    });
}
