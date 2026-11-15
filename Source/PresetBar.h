#pragma once

#include "PluginProcessor.h"
#include "PresetManager.h"

/** Header control: [<]  preset name (click for the menu)  [>] */
class PresetBar : public juce::Component,
                  private juce::Timer
{
public:
    explicit PresetBar (SimpleSamplerProcessor&);
    ~PresetBar() override;

    /** Re-reads the current preset name from the processor. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override      { repaint(); }
    void mouseExit (const juce::MouseEvent&) override       { repaint(); }

private:
    /** Small round button with a chevron drawn as a path. */
    class ChevronButton : public juce::Button
    {
    public:
        ChevronButton (const juce::String& name, bool pointsRight) : juce::Button (name), right (pointsRight) {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    private:
        bool right;
    };

    void timerCallback() override;
    void showMenu();
    void beginNaming();
    void finishNaming (bool save);
    void saveAs (const juce::String& name);
    void flash (const juce::String& message);
    juce::Rectangle<int> nameArea() const;

    SimpleSamplerProcessor& processor;
    PresetManager presets;

    ChevronButton previousButton { "Previous preset", false }, nextButton { "Next preset", true };
    std::unique_ptr<juce::TextEditor> nameEditor;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::String currentName, flashText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};
