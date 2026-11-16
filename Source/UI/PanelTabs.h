#pragma once

#include "../PluginProcessor.h"

/** Row above the right panel: the four tabs and the four draggable modulator chips. */
class PanelTabs : public juce::Component,
                  private juce::Timer
{
public:
    enum Tab { sampleTab = 0, toneTab, genreTab, modTab, numTabs };

    explicit PanelTabs (SimpleSamplerProcessor&);
    ~PanelTabs() override;

    void setTab (int tab);
    int getTab() const noexcept                             { return currentTab; }
    void setSelectedMod (int index);

    std::function<void (int)> onTabChanged;
    /** A chip was clicked (not dragged). */
    std::function<void (int)> onModSelected;

    void resized() override;

private:
    /** "M1".."M4": drag it onto a knob to assign the modulator; shows the modulator's current output. */
    class ModChip : public juce::Component,
                    public juce::SettableTooltipClient
    {
    public:
        ModChip (PanelTabs& o, int i) : owner (o), index (i)
        {
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        }
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
        void mouseExit (const juce::MouseEvent&) override   { repaint(); }

        float level = 0.0f;
        bool selected = false, used = false;

    private:
        PanelTabs& owner;
        const int index;
        bool dragStarted = false;
    };

    void timerCallback() override;

    SimpleSamplerProcessor& processor;
    juce::OwnedArray<juce::TextButton> tabs;
    juce::OwnedArray<ModChip> chips;
    int currentTab = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanelTabs)
};
