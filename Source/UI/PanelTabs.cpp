#include "PanelTabs.h"
#include "Knob.h"
#include "../PastelLookAndFeel.h"

PanelTabs::PanelTabs (SimpleSamplerProcessor& p) : processor (p)
{
    static const char* const names[numTabs] = { "Sample", "Tone/FX", "Genre", "Mod" };
    for (int i = 0; i < numTabs; ++i)
    {
        auto* b = tabs.add (new juce::TextButton (names[i]));
        b->setClickingTogglesState (false);
        b->onClick = [this, i]
        {
            setTab (i);
            if (onTabChanged) onTabChanged (i);
        };
        addAndMakeVisible (b);
    }

    for (int i = 0; i < SimpleSamplerProcessor::numMods; ++i)
    {
        auto* c = chips.add (new ModChip (*this, i));
        c->setTooltip ("Modulator " + juce::String (i + 1) + ": drag onto a knob to assign it");
        addAndMakeVisible (c);
    }

    setTab (0);
    startTimerHz (30);
}

PanelTabs::~PanelTabs()
{
    stopTimer();
}

void PanelTabs::setTab (int tab)
{
    currentTab = juce::jlimit (0, numTabs - 1, tab);
    for (int i = 0; i < tabs.size(); ++i)
        tabs[i]->setToggleState (i == currentTab, juce::dontSendNotification);
}

void PanelTabs::setSelectedMod (int index)
{
    for (int i = 0; i < chips.size(); ++i)
    {
        if (chips[i]->selected != (i == index))
        {
            chips[i]->selected = (i == index);
            chips[i]->repaint();
        }
    }
}

void PanelTabs::resized()
{
    auto area = getLocalBounds();

    // chips on the right, tabs share the rest
    const int chipWidth = 30, chipGap = 4;
    auto chipArea = area.removeFromRight (chips.size() * (chipWidth + chipGap));
    for (auto* c : chips)
    {
        chipArea.removeFromLeft (chipGap);
        c->setBounds (chipArea.removeFromLeft (chipWidth).reduced (0, 3));
    }

    area.removeFromRight (6);
    const int gap = 4;
    const int w = (area.getWidth() - gap * (tabs.size() - 1)) / juce::jmax (1, tabs.size());
    for (auto* t : tabs)
    {
        t->setBounds (area.removeFromLeft (w).reduced (0, 2));
        area.removeFromLeft (gap);
    }
}

void PanelTabs::timerCallback()
{
    if (! isShowing())
        return;

    const auto slots = processor.getModMatrix().getSlots();
    for (int i = 0; i < chips.size(); ++i)
    {
        bool used = false;
        for (const auto& s : slots)
            used = used || s.source == i;

        const float level = processor.getModulator (i).uiValue.load (std::memory_order_relaxed);
        auto* c = chips[i];
        if (std::abs (level - c->level) > 0.01f || used != c->used)
        {
            c->level = level;
            c->used = used;
            c->repaint();
        }
    }
}

//==============================================================================
void PanelTabs::ModChip::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto colour = PastelLookAndFeel::modColour (index);
    const float radius = 8.0f;

    g.setColour (juce::Colours::white.withAlpha (isMouseOver() ? 0.98f : 0.8f));
    g.fillRoundedRectangle (bounds, radius);

    // current output: a bar growing from the middle, up for positive and down for negative values
    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addRoundedRectangle (bounds, radius);
        g.reduceClipRegion (clip);
        const float mid = bounds.getCentreY();
        const float h = bounds.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, level);
        g.setColour (colour.withAlpha (0.30f));
        g.fillRect (juce::Rectangle<float> (bounds.getX(), h >= 0.0f ? mid - h : mid, bounds.getWidth(), std::abs (h)));
    }

    g.setColour (selected ? colour : colour.withAlpha (used ? 0.75f : 0.4f));
    g.drawRoundedRectangle (bounds, radius, selected ? 2.0f : 1.2f);

    g.setColour (colour.darker (0.25f));
    g.setFont (PastelLookAndFeel::font (11.5f, true));
    g.drawText ("M" + juce::String (index + 1), getLocalBounds(), juce::Justification::centred, false);
}

void PanelTabs::ModChip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() < 5)
        return;

    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragStarted = true;
        container->startDragging (juce::String (Knob::dragPrefix()) + juce::String (index), this);
    }
}

void PanelTabs::ModChip::mouseUp (const juce::MouseEvent& e)
{
    if (! dragStarted && e.mouseWasClicked() && owner.onModSelected)
        owner.onModSelected (index);
    dragStarted = false;
}
