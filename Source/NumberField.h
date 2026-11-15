#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <limits>

/** A small rounded value box with a caption.
    Drag up/down or use the mouse wheel to change the value, click to type one. */
class NumberField : public juce::Component
{
public:
    NumberField();

    void setCaption (const juce::String& newCaption)        { caption = newCaption; repaint(); }
    /** `interval` is the grid typed values snap to, `dragStep` the step used when dragging. */
    void setRange (double minimum, double maximum, double interval, double dragStep);
    void setPixelsPerStep (double pixels)                   { pixelsPerStep = pixels; }
    void setDefaultValue (double v)                         { defaultValue = v; hasDefault = true; }

    /** Sets the shown value without calling onValueChange. */
    void setValue (double newValue);
    double getValue() const noexcept                        { return value; }

    std::function<juce::String (double)> textFromValue;
    /** Should return NaN for text that cannot be understood. */
    std::function<double (const juce::String&)> valueFromText;

    std::function<void()> onGestureStart, onGestureEnd;
    std::function<void (double)> onValueChange;             // only for changes made by the user

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override      { repaint(); }
    void mouseExit (const juce::MouseEvent&) override       { repaint(); }

    static constexpr int captionHeight = 15;

private:
    juce::Rectangle<int> boxArea() const;
    double constrain (double v, double grid) const;
    void userSet (double v, double grid);
    void showEditor();
    void hideEditor (bool commit);

    juce::String caption;
    double value = 0.0, minValue = 0.0, maxValue = 1.0, interval = 0.0, dragStep = 1.0;
    double pixelsPerStep = 4.0, dragStartValue = 0.0, defaultValue = 0.0;
    bool hasDefault = false, dragging = false, wheelGesture = false;
    std::unique_ptr<juce::TextEditor> editor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NumberField)
};

/** Connects a NumberField to a plugin parameter (both directions, with automation gestures). */
class ParamField
{
public:
    ParamField (juce::RangedAudioParameter& parameter, NumberField& fieldToUse, std::function<void()> onParameterChanged = {});
    ~ParamField();

    double get() const                                      { return (double) param.convertFrom0to1 (param.getValue()); }
    void beginGesture()                                     { attachment.beginGesture(); }
    void endGesture()                                       { attachment.endGesture(); }
    void setDuringGesture (double v)                        { attachment.setValueAsPartOfGesture ((float) v); }
    void setComplete (double v)                             { attachment.setValueAsCompleteGesture ((float) v); }

private:
    juce::RangedAudioParameter& param;
    NumberField& field;
    std::function<void()> changed;
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamField)
};
