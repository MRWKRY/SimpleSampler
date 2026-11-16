#pragma once

#include "../PluginProcessor.h"

/** Thin-arc knob bound to a parameter.

    - drag = change, Shift + drag = fine, double-click = default, mouse wheel = small steps
    - hovering shows the value instead of the name
    - a modulator chip can be dropped on it; the outer arc then shows the modulation range
      and a dot shows where the value currently is
    - right-click: reset, randomiser lock, modulation depth / removal */
class Knob : public juce::Component,
             public juce::DragAndDropTarget,
             private juce::Timer
{
public:
    Knob (SimpleSamplerProcessor&, const juce::String& labelText);
    ~Knob() override;

    /** Binds the knob to a parameter (an empty ID unbinds it). */
    void setParameter (const juce::String& paramID);

    static constexpr int preferredHeight = 62;
    static const char* dragPrefix()                         { return "mod:"; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override      { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override       { hover = false; repaint(); }
    void enablementChanged() override                       { repaint(); }

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override      { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override       { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

private:
    void timerCallback() override;
    void showMenu();
    void setNormalised (float newValue);

    SimpleSamplerProcessor& processor;
    juce::String label;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int target = -1;                    // index in the ModMatrix, -1 = not modulatable

    float value = 0.0f, dragValue = 0.0f;       // normalised
    juce::Point<float> lastDrag;
    bool dragging = false, hover = false, dropHover = false, bipolar = false;

    // modulation display (refreshed by the timer)
    uint32_t matrixVersion = 0xffffffffu;
    float totalDepth = 0.0f, liveOffset = 0.0f;
    int firstSource = -1;
    bool locked = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

/** Lays knobs out in one row with equal widths. */
void layoutKnobRow (juce::Rectangle<int> row, std::initializer_list<juce::Component*> items);
