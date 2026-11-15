#pragma once

#include "PluginProcessor.h"

class PadGrid;

/** One pad: click to select + play, drop a file to assign it, right-click for a menu. */
class PadButton : public juce::Component,
                  public juce::FileDragAndDropTarget
{
public:
    PadButton (PadGrid& owner, SimpleSamplerProcessor&, int padIndex);

    void refreshContent();
    void setSelected (bool shouldBeSelected);
    /** Advances the glow animation; returns true if a repaint was needed. */
    bool updateGlow();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override      { repaint(); }
    void mouseExit (const juce::MouseEvent&) override       { repaint(); }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override    { dragOver = true;  repaint(); }
    void fileDragExit (const juce::StringArray&) override               { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    PadGrid& grid;
    SimpleSamplerProcessor& processor;
    const int pad;

    juce::String name;
    int midiNote = -1;
    bool hasSample = false, hasFile = false, missing = false, loading = false;
    bool selected = false, dragOver = false, pressed = false;
    float glow = 0.0f;
    uint32_t lastTrigger = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadButton)
};

/** The 4x4 grid. Pad 1 is bottom-left, like a drum-pad controller. */
class PadGrid : public juce::Component
{
public:
    explicit PadGrid (SimpleSamplerProcessor&);

    void setSelectedPad (int padIndex);
    void refreshPad (int padIndex);
    void refreshAll();
    void updateGlow();

    std::function<void (int)> onSelect;     // user selected a pad
    std::function<void (int)> onBrowse;     // user wants a file chooser for a pad

    void resized() override;

private:
    SimpleSamplerProcessor& processor;
    juce::OwnedArray<PadButton> buttons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadGrid)
};
