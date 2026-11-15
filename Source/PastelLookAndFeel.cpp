#include "PastelLookAndFeel.h"

using juce::Colour;
using juce::Graphics;
using juce::Rectangle;

PastelLookAndFeel::PastelLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Colour (0xfff3f0fb));

    setColour (juce::Label::textColourId, text());
    setColour (juce::Label::textWhenEditingColourId, text());

    setColour (juce::TextButton::buttonColourId, juce::Colours::white.withAlpha (0.75f));
    setColour (juce::TextButton::buttonOnColourId, accent());
    setColour (juce::TextButton::textColourOffId, text());
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);

    setColour (juce::ToggleButton::textColourId, text());
    setColour (juce::ToggleButton::tickColourId, accent());

    setColour (juce::ComboBox::backgroundColourId, juce::Colours::white.withAlpha (0.8f));
    setColour (juce::ComboBox::textColourId, text());
    setColour (juce::ComboBox::outlineColourId, track());
    setColour (juce::ComboBox::arrowColourId, textSoft());

    setColour (juce::PopupMenu::backgroundColourId, Colour (0xfffbfaff));
    setColour (juce::PopupMenu::textColourId, text());
    setColour (juce::PopupMenu::headerTextColourId, textSoft());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent().withAlpha (0.14f));
    setColour (juce::PopupMenu::highlightedTextColourId, text());

    setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
    setColour (juce::TextEditor::textColourId, text());
    setColour (juce::TextEditor::highlightColourId, accentSoft().withAlpha (0.6f));
    setColour (juce::TextEditor::highlightedTextColourId, text());
    setColour (juce::TextEditor::outlineColourId, track());
    setColour (juce::TextEditor::focusedOutlineColourId, accent());
    setColour (juce::CaretComponent::caretColourId, accent());

    setColour (juce::AlertWindow::backgroundColourId, Colour (0xfffbfaff));
    setColour (juce::AlertWindow::textColourId, text());
    setColour (juce::AlertWindow::outlineColourId, track());

    setColour (juce::Slider::rotarySliderFillColourId, accent());
    setColour (juce::Slider::rotarySliderOutlineColourId, track());
    setColour (juce::Slider::textBoxTextColourId, text());
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    setColour (juce::TooltipWindow::backgroundColourId, Colour (0xff4a4860));
    setColour (juce::TooltipWindow::textColourId, juce::Colours::white);
    setColour (juce::TooltipWindow::outlineColourId, juce::Colours::transparentBlack);
}

juce::Font PastelLookAndFeel::font (float height, bool bold)
{
    return juce::Font (juce::FontOptions ("Segoe UI", height, bold ? juce::Font::bold : juce::Font::plain));
}

PastelLookAndFeel::PadColours PastelLookAndFeel::padColours (int padIndex)
{
    static const juce::uint32 table[16][2] =
    {
        { 0xff8b5cf6, 0xff60c5f1 },   // purple -> sky
        { 0xfff472b6, 0xfffb923c },   // pink -> orange
        { 0xff38bdf8, 0xff34d399 },   // sky -> mint
        { 0xfffb7185, 0xffc084fc },   // rose -> lilac
        { 0xff6366f1, 0xffe879f9 },   // indigo -> magenta
        { 0xfffbbf24, 0xfff472b6 },   // amber -> pink
        { 0xff2dd4bf, 0xff818cf8 },   // teal -> periwinkle
        { 0xffa78bfa, 0xfffda4af },   // violet -> blush
        { 0xff22d3ee, 0xff6366f1 },   // cyan -> indigo
        { 0xfffb923c, 0xfffacc15 },   // orange -> yellow
        { 0xffc084fc, 0xff38bdf8 },   // lilac -> sky
        { 0xff4ade80, 0xff22d3ee },   // green -> cyan
        { 0xfff43f5e, 0xfffb923c },   // red-rose -> orange
        { 0xff818cf8, 0xff2dd4bf },   // periwinkle -> teal
        { 0xffe879f9, 0xfffbbf24 },   // magenta -> amber
        { 0xff60a5fa, 0xfff472b6 }    // blue -> pink
    };
    const int i = ((padIndex % 16) + 16) % 16;
    return { Colour (table[i][0]), Colour (table[i][1]) };
}

//==============================================================================
void PastelLookAndFeel::drawBackground (Graphics& g, Rectangle<int> bounds)
{
    const auto b = bounds.toFloat();

    // base: lavender -> soft pink diagonal
    g.setGradientFill (juce::ColourGradient (Colour (0xffece6fb), b.getX(), b.getY(),
                                             Colour (0xfffbe8f2), b.getRight(), b.getBottom(), false));
    g.fillRect (b);

    // light blue glow from the top right, white glow in the centre, fading towards the edges
    {
        juce::ColourGradient glow (Colour (0xb0dff1fd), b.getRight() * 0.92f, b.getY() + b.getHeight() * 0.05f,
                                   Colour (0x00dff1fd), b.getRight() * 0.92f - b.getWidth() * 0.65f, b.getY(), true);
        g.setGradientFill (glow);
        g.fillRect (b);
    }
    {
        juce::ColourGradient glow (Colour (0x90ffffff), b.getCentreX() * 0.8f, b.getCentreY(),
                                   Colour (0x00ffffff), b.getCentreX() * 0.8f + b.getWidth() * 0.55f, b.getCentreY(), true);
        g.setGradientFill (glow);
        g.fillRect (b);
    }
    {
        juce::ColourGradient glow (Colour (0x70e4dcff), b.getX(), b.getBottom(),
                                   Colour (0x00e4dcff), b.getX() + b.getWidth() * 0.5f, b.getBottom(), true);
        g.setGradientFill (glow);
        g.fillRect (b);
    }

    // thin grid
    g.setColour (gridLine());
    const int step = 28;
    for (int x = bounds.getX() + step; x < bounds.getRight(); x += step)
        g.fillRect (x, bounds.getY(), 1, bounds.getHeight());
    for (int y = bounds.getY() + step; y < bounds.getBottom(); y += step)
        g.fillRect (bounds.getX(), y, bounds.getWidth(), 1);
}

void PastelLookAndFeel::drawCard (Graphics& g, Rectangle<float> bounds, float radius, float opacity)
{
    // very soft shadow: a few faint, slightly offset outlines
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (Colour (0xff6b5ca5).withAlpha (0.018f));
        g.fillRoundedRectangle (bounds.expanded ((float) i * 1.5f).translated (0.0f, (float) i * 1.2f), radius + (float) i * 1.5f);
    }

    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (juce::jmin (1.0f, opacity + 0.16f)), bounds.getX(), bounds.getY(),
                                             juce::Colours::white.withAlpha (opacity), bounds.getX(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, radius);

    g.setColour (juce::Colours::white.withAlpha (0.95f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
}

void PastelLookAndFeel::drawFieldBox (Graphics& g, Rectangle<float> bounds, bool highlighted)
{
    const float radius = juce::jmin (10.0f, bounds.getHeight() * 0.5f);
    g.setColour (juce::Colours::white.withAlpha (highlighted ? 1.0f : 0.78f));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (highlighted ? accent().withAlpha (0.7f) : track());
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
}

//==============================================================================
void PastelLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height, float sliderPos,
                                          float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float thickness = juce::jlimit (2.0f, 3.5f, radius * 0.16f);
    const float arcRadius = radius - thickness * 0.5f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const bool active = slider.isEnabled();

    juce::Path trackArc;
    trackArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (track());
    g.strokePath (trackArc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path valueArc;
    valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
    g.setColour (active ? accent() : textSoft());
    g.strokePath (valueArc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // white knob body with a small pointer dot
    const float bodyRadius = arcRadius - thickness - 2.5f;
    if (bodyRadius > 3.0f)
    {
        g.setColour (Colour (0xff6b5ca5).withAlpha (0.10f));
        g.fillEllipse (Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre.translated (0.0f, 1.5f)));
        g.setColour (juce::Colours::white);
        g.fillEllipse (Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre));

        const float dotRadius = juce::jmax (1.5f, bodyRadius * 0.14f);
        const float dotDistance = bodyRadius - dotRadius * 2.4f;
        const juce::Point<float> dot (centre.x + dotDistance * std::sin (angle), centre.y - dotDistance * std::cos (angle));
        g.setColour (active ? accent() : textSoft());
        g.fillEllipse (Rectangle<float> (dotRadius * 2.0f, dotRadius * 2.0f).withCentre (dot));
    }
}

void PastelLookAndFeel::drawToggleButton (Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const float h = juce::jmin (20.0f, bounds.getHeight());
    const float w = h * 1.8f;
    const Rectangle<float> pill (bounds.getX() + 1.0f, bounds.getCentreY() - h * 0.5f, w, h);
    const bool on = button.getToggleState();

    g.setColour (on ? accent() : track().darker (highlighted ? 0.08f : 0.0f));
    g.fillRoundedRectangle (pill, h * 0.5f);

    const float knob = h - 5.0f;
    const float kx = on ? pill.getRight() - knob - 2.5f : pill.getX() + 2.5f;
    g.setColour (Colour (0xff3d3b4f).withAlpha (0.12f));
    g.fillEllipse (kx, pill.getCentreY() - knob * 0.5f + 1.0f, knob, knob);
    g.setColour (juce::Colours::white);
    g.fillEllipse (kx, pill.getCentreY() - knob * 0.5f, knob, knob);

    g.setColour (button.findColour (juce::ToggleButton::textColourId).withAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.setFont (font (13.5f));
    g.drawText (button.getButtonText(), bounds.withTrimmedLeft (w + 9.0f), juce::Justification::centredLeft, true);
}

void PastelLookAndFeel::drawButtonBackground (Graphics& g, juce::Button& button, const Colour&, bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = juce::jmin (10.0f, bounds.getHeight() * 0.5f);
    const bool on = button.getToggleState();

    auto fill = on ? accent() : juce::Colours::white.withAlpha (highlighted ? 0.98f : 0.74f);
    if (down) fill = on ? accent().darker (0.12f) : accentSoft().withAlpha (0.45f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (on ? accent() : (highlighted ? accentSoft() : track()));
    g.drawRoundedRectangle (bounds, radius, 1.0f);
}

void PastelLookAndFeel::drawButtonText (Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId)
                     .withAlpha (button.isEnabled() ? 1.0f : 0.4f));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (6, 0), juce::Justification::centred, 1);
}

juce::Font PastelLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return font (juce::jmin (14.0f, (float) buttonHeight * 0.55f));
}

void PastelLookAndFeel::drawComboBox (Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
    const float radius = juce::jmin (10.0f, bounds.getHeight() * 0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (box.hasKeyboardFocus (true) ? accentSoft() : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, radius, 1.0f);

    const float cx = (float) width - 14.0f, cy = (float) height * 0.5f;
    juce::Path arrow;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.5f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font PastelLookAndFeel::getComboBoxFont (juce::ComboBox&)        { return font (13.5f); }

void PastelLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

//==============================================================================
void PastelLookAndFeel::drawPopupMenuBackground (Graphics& g, int width, int height)
{
    const auto bounds = Rectangle<int> (0, 0, width, height).toFloat();
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (track());
    g.drawRect (bounds, 1.0f);
}

void PastelLookAndFeel::drawPopupMenuItem (Graphics& g, const Rectangle<int>& area, bool isSeparator, bool isActive,
                                           bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& itemText,
                                           const juce::String&, const juce::Drawable*, const Colour*)
{
    if (isSeparator)
    {
        g.setColour (track());
        g.fillRect (area.reduced (10, 0).withSizeKeepingCentre (area.getWidth() - 20, 1));
        return;
    }

    auto r = area.reduced (4, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (findColour (juce::PopupMenu::highlightedBackgroundColourId));
        g.fillRoundedRectangle (r.toFloat(), 7.0f);
    }

    auto textArea = r.reduced (10, 0);
    if (isTicked)
    {
        g.setColour (accent());
        g.fillEllipse (Rectangle<float> (6.0f, 6.0f).withCentre ({ (float) textArea.getX() + 3.0f, (float) textArea.getCentreY() }));
    }
    textArea.removeFromLeft (14);

    g.setColour (text().withAlpha (isActive ? 1.0f : 0.4f));
    g.setFont (getPopupMenuFont());
    g.drawFittedText (itemText, textArea, juce::Justification::centredLeft, 1);

    if (hasSubMenu)
    {
        const float cx = (float) r.getRight() - 12.0f, cy = (float) r.getCentreY();
        juce::Path arrow;
        arrow.startNewSubPath (cx - 2.0f, cy - 4.0f);
        arrow.lineTo (cx + 2.0f, cy);
        arrow.lineTo (cx - 2.0f, cy + 4.0f);
        g.setColour (textSoft());
        g.strokePath (arrow, juce::PathStrokeType (1.4f));
    }
}

juce::Font PastelLookAndFeel::getPopupMenuFont()                        { return font (14.0f); }
juce::Font PastelLookAndFeel::getLabelFont (juce::Label& l)             { return font (juce::jmin (15.0f, (float) l.getHeight() * 0.7f)); }
juce::Font PastelLookAndFeel::getAlertWindowTitleFont()                 { return font (17.0f, true); }
juce::Font PastelLookAndFeel::getAlertWindowMessageFont()               { return font (14.0f); }
juce::Font PastelLookAndFeel::getAlertWindowFont()                      { return font (14.0f); }

void PastelLookAndFeel::drawAlertBox (Graphics& g, juce::AlertWindow& alert, const Rectangle<int>& textArea, juce::TextLayout& layout)
{
    const auto bounds = alert.getLocalBounds().toFloat();
    g.setColour (findColour (juce::AlertWindow::backgroundColourId));
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (track());
    g.drawRoundedRectangle (bounds.reduced (0.5f), 12.0f, 1.0f);

    g.setColour (text());
    layout.draw (g, textArea.toFloat());
}

void PastelLookAndFeel::fillTextEditorBackground (Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, juce::jmin (9.0f, (float) height * 0.5f));
}

void PastelLookAndFeel::drawTextEditorOutline (Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.hasKeyboardFocus (true) ? accent() : track());
    g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f, juce::jmin (9.0f, (float) height * 0.5f), 1.0f);
}
