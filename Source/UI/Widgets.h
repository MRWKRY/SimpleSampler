#pragma once

#include "../PluginProcessor.h"
#include "../NumberField.h"
#include "../PastelLookAndFeel.h"

/** Combo box with a small caption above it (same proportions as NumberField), bound to a choice parameter. */
class CaptionCombo : public juce::Component
{
public:
    explicit CaptionCombo (const juce::String& captionText) : caption (captionText)
    {
        addAndMakeVisible (box);
    }

    /** Fills the list from the parameter's choices and keeps both in sync. */
    void bind (juce::RangedAudioParameter* param)
    {
        attachment.reset();
        box.clear (juce::dontSendNotification);
        if (param == nullptr)
            return;
        box.addItemList (param->getAllValueStrings(), 1);
        attachment = std::make_unique<juce::ComboBoxParameterAttachment> (*param, box);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (PastelLookAndFeel::textSoft().withAlpha (isEnabled() ? 1.0f : 0.5f));
        g.setFont (PastelLookAndFeel::font (11.0f));
        g.drawText (caption, getLocalBounds().removeFromTop (NumberField::captionHeight).withTrimmedLeft (2),
                    juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        box.setBounds (getLocalBounds().withTrimmedTop (NumberField::captionHeight));
    }

    juce::ComboBox box;

private:
    juce::String caption;
    std::unique_ptr<juce::ComboBoxParameterAttachment> attachment;
};

/** Sets a NumberField up for a float parameter (range, value text from the parameter) and connects it. */
inline std::unique_ptr<ParamField> attachField (NumberField& field, juce::RangedAudioParameter* param, double dragStep)
{
    if (param == nullptr)
        return {};

    const auto& range = param->getNormalisableRange();
    field.setRange (range.start, range.end, range.interval, dragStep);
    field.textFromValue = [param] (double v) { return param->getText (param->convertTo0to1 ((float) v), 32); };
    field.valueFromText = [] (const juce::String& text)
    {
        const auto t = text.trim().replaceCharacter (',', '.');
        if (! t.containsAnyOf ("0123456789"))
            return std::numeric_limits<double>::quiet_NaN();
        return t.retainCharacters ("0123456789.-+").getDoubleValue();
    };
    return std::make_unique<ParamField> (*param, field);
}

constexpr int fieldRowHeight = NumberField::captionHeight + 26;
