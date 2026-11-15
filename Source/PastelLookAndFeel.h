#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Light pastel look: translucent white rounded cards, thin arcs, purple accent. */
class PastelLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PastelLookAndFeel();

    // ---- palette -----------------------------------------------------------
    static juce::Colour text()        { return juce::Colour (0xff3d3b4f); }
    static juce::Colour textSoft()    { return juce::Colour (0xff9896ad); }
    static juce::Colour accent()      { return juce::Colour (0xff8b5cf6); }
    static juce::Colour accentSoft()  { return juce::Colour (0xffcbbcfb); }
    static juce::Colour track()       { return juce::Colour (0xffe3e1ee); }
    static juce::Colour gridLine()    { return juce::Colour (0x12605a8a); }

    struct PadColours { juce::Colour a, b; };
    /** Two-colour gradient of a pad (each pad has its own hue pair). */
    static PadColours padColours (int padIndex);

    static juce::Font font (float height, bool bold = false);

    // ---- shared drawing helpers ---------------------------------------------
    static void drawBackground (juce::Graphics&, juce::Rectangle<int> bounds);
    static void drawCard (juce::Graphics&, juce::Rectangle<float> bounds, float radius = 14.0f, float opacity = 0.62f);
    static void drawFieldBox (juce::Graphics&, juce::Rectangle<float> bounds, bool highlighted);

    // ---- LookAndFeel overrides ----------------------------------------------
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;
    int getPopupMenuBorderSize() override                   { return 6; }

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea,
                       juce::TextLayout&) override;
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PastelLookAndFeel)
};
