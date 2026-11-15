#include "NumberField.h"
#include "PastelLookAndFeel.h"

NumberField::NumberField()
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setWantsKeyboardFocus (false);
}

void NumberField::setRange (double minimum, double maximum, double newInterval, double newDragStep)
{
    minValue = minimum;
    maxValue = juce::jmax (minimum, maximum);
    interval = newInterval;
    dragStep = newDragStep > 0.0 ? newDragStep : 1.0;
    value = juce::jlimit (minValue, maxValue, value);
    repaint();
}

double NumberField::constrain (double v, double grid) const
{
    if (grid > 0.0)
        v = std::round (v / grid) * grid;
    return juce::jlimit (minValue, maxValue, v);
}

void NumberField::setValue (double newValue)
{
    newValue = juce::jlimit (minValue, maxValue, newValue);
    if (std::abs (newValue - value) > 1.0e-9)
    {
        value = newValue;
        repaint();
    }
}

void NumberField::userSet (double v, double grid)
{
    v = constrain (v, grid);
    if (std::abs (v - value) < 1.0e-9)
        return;

    value = v;
    repaint();
    if (onValueChange)
        onValueChange (value);
}

juce::Rectangle<int> NumberField::boxArea() const
{
    auto r = getLocalBounds();
    if (caption.isNotEmpty())
        r.removeFromTop (captionHeight);
    return r;
}

void NumberField::paint (juce::Graphics& g)
{
    if (caption.isNotEmpty())
    {
        g.setColour (PastelLookAndFeel::textSoft());
        g.setFont (PastelLookAndFeel::font (11.0f));
        g.drawText (caption, getLocalBounds().removeFromTop (captionHeight).withTrimmedLeft (2),
                    juce::Justification::centredLeft, true);
    }

    const auto box = boxArea().toFloat();
    PastelLookAndFeel::drawFieldBox (g, box, dragging || (isMouseOver() && isEnabled()));

    if (editor == nullptr)
    {
        g.setColour (PastelLookAndFeel::text().withAlpha (isEnabled() ? 1.0f : 0.4f));
        g.setFont (PastelLookAndFeel::font (juce::jmin (15.0f, box.getHeight() * 0.56f), true));
        const auto t = textFromValue ? textFromValue (value) : juce::String (value, 2);
        g.drawFittedText (t, boxArea().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
    }
}

void NumberField::resized()
{
    if (editor != nullptr)
        editor->setBounds (boxArea());
}

void NumberField::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled() || e.mods.isPopupMenu())
        return;

    dragStartValue = value;
    dragging = false;
}

void NumberField::mouseDrag (const juce::MouseEvent& e)
{
    if (! isEnabled() || e.mods.isPopupMenu())
        return;

    if (! dragging)
    {
        if (e.getDistanceFromDragStart() < 3)
            return;

        dragging = true;
        if (onGestureStart) onGestureStart();
        repaint();
    }

    // up = more; shift = fine
    const double fine = e.mods.isShiftDown() ? 0.1 : 1.0;
    const double steps = -e.getDistanceFromDragStartY() / pixelsPerStep;
    const double grid = dragStep * fine;
    userSet (dragStartValue + std::round (steps) * grid, e.mods.isShiftDown() ? 0.0 : dragStep);
}

void NumberField::mouseUp (const juce::MouseEvent& e)
{
    if (! isEnabled() || e.mods.isPopupMenu())
        return;

    if (dragging)
    {
        dragging = false;
        if (onGestureEnd) onGestureEnd();
        repaint();
    }
    else if (e.mouseWasClicked() && boxArea().contains (e.getPosition()) && e.getNumberOfClicks() == 1)
    {
        showEditor();
    }
}

void NumberField::mouseDoubleClick (const juce::MouseEvent& e)
{
    // alt/ctrl + double-click: back to the default value
    if (hasDefault && isEnabled() && (e.mods.isAltDown() || e.mods.isCommandDown()))
    {
        hideEditor (false);
        if (onGestureStart) onGestureStart();
        userSet (defaultValue, 0.0);
        if (onGestureEnd) onGestureEnd();
    }
}

void NumberField::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! isEnabled() || editor != nullptr || std::abs (wheel.deltaY) < 1.0e-4f)
    {
        Component::mouseWheelMove (e, wheel);
        return;
    }

    const double direction = (wheel.deltaY > 0 ? 1.0 : -1.0) * (wheel.isReversed ? -1.0 : 1.0);
    const double step = dragStep * (e.mods.isShiftDown() ? 0.1 : 1.0);

    if (onGestureStart) onGestureStart();
    userSet (value + direction * step, e.mods.isShiftDown() ? 0.0 : dragStep);
    if (onGestureEnd) onGestureEnd();
}

void NumberField::showEditor()
{
    if (editor != nullptr)
        return;

    editor = std::make_unique<juce::TextEditor>();
    editor->setJustification (juce::Justification::centred);
    editor->setFont (PastelLookAndFeel::font (juce::jmin (15.0f, (float) boxArea().getHeight() * 0.56f), true));
    editor->setText (textFromValue ? textFromValue (value) : juce::String (value, 2), false);
    editor->setSelectAllWhenFocused (true);
    editor->setEscapeAndReturnKeysConsumed (true);
    addAndMakeVisible (*editor);
    editor->setBounds (boxArea());

    juce::Component::SafePointer<NumberField> safe (this);
    editor->onReturnKey = [safe] { if (safe != nullptr) safe->hideEditor (true); };
    editor->onEscapeKey = [safe] { if (safe != nullptr) safe->hideEditor (false); };
    editor->onFocusLost = [safe] { if (safe != nullptr) safe->hideEditor (true); };
    editor->grabKeyboardFocus();
    repaint();
}

void NumberField::hideEditor (bool commit)
{
    if (editor == nullptr)
        return;

    const auto typed = editor->getText();
    // the editor may be in the middle of its own callback: delete it a little later
    auto dying = std::shared_ptr<juce::TextEditor> (editor.release());
    dying->onReturnKey = nullptr;
    dying->onEscapeKey = nullptr;
    dying->onFocusLost = nullptr;
    dying->setVisible (false);
    juce::MessageManager::callAsync ([dying]() mutable { dying.reset(); });

    if (commit)
    {
        double v = std::numeric_limits<double>::quiet_NaN();
        if (valueFromText)
            v = valueFromText (typed);
        else if (typed.containsAnyOf ("0123456789"))
            v = typed.retainCharacters ("0123456789.-+").getDoubleValue();

        if (! std::isnan (v))
        {
            if (onGestureStart) onGestureStart();
            userSet (v, interval);
            if (onGestureEnd) onGestureEnd();
        }
    }
    repaint();
}

//==============================================================================
ParamField::ParamField (juce::RangedAudioParameter& parameter, NumberField& fieldToUse, std::function<void()> onParameterChanged)
    : param (parameter), field (fieldToUse), changed (std::move (onParameterChanged)),
      attachment (parameter, [this] (float v)
                  {
                      field.setValue ((double) v);
                      if (changed) changed();
                  })
{
    field.onGestureStart = [this] { attachment.beginGesture(); };
    field.onGestureEnd   = [this] { attachment.endGesture(); };
    field.onValueChange  = [this] (double v) { attachment.setValueAsPartOfGesture ((float) v); };
    field.setDefaultValue ((double) param.convertFrom0to1 (param.getDefaultValue()));
    attachment.sendInitialUpdate();
}

ParamField::~ParamField()
{
    field.onGestureStart = nullptr;
    field.onGestureEnd = nullptr;
    field.onValueChange = nullptr;
}
