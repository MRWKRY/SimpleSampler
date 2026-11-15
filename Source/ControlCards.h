#pragma once

#include "PluginProcessor.h"
#include "NumberField.h"
#include "EnvelopeEditor.h"

/** White rounded card with a title; base class of the four cards in the right panel. */
class Card : public juce::Component
{
public:
    Card (SimpleSamplerProcessor&, const juce::String& title);

    virtual void setPad (int padIndex) = 0;
    void paint (juce::Graphics&) override;

protected:
    juce::Rectangle<int> contentArea() const;
    /** Lays out fields in one row with equal widths. */
    static void layoutRow (juce::Rectangle<int> row, std::initializer_list<juce::Component*> items, int gap = 8);

    SimpleSamplerProcessor& processor;
    juce::String title, summary;        // summary is drawn right-aligned in the title row
    int pad = 0;

    static constexpr int titleHeight = 24;
};

//==============================================================================
class BpmCard : public Card
{
public:
    explicit BpmCard (SimpleSamplerProcessor&);
    void setPad (int padIndex) override;
    void resized() override;

private:
    void refresh();

    NumberField startField, resultField, diffField;
    std::unique_ptr<ParamField> startParam, resultParam;
};

//==============================================================================
class PitchCard : public Card
{
public:
    explicit PitchCard (SimpleSamplerProcessor&);
    void setPad (int padIndex) override;
    void resized() override;

private:
    void refresh();

    NumberField startField, resultField, semitoneField;
    std::unique_ptr<ParamField> startParam, semitoneParam;
};

//==============================================================================
class LoopCard : public Card
{
public:
    explicit LoopCard (SimpleSamplerProcessor&);
    void setPad (int padIndex) override;
    void resized() override;

    /** Length of the pad's sample (used to show the loop points in seconds). */
    void setSampleLength (double seconds);

private:
    void refresh();

    juce::ToggleButton loopToggle { "Loop" };
    NumberField startField, endField;
    std::unique_ptr<juce::ButtonParameterAttachment> toggleAttachment;
    std::unique_ptr<ParamField> startParam, endParam;
    double lengthSeconds = 0.0;
};

//==============================================================================
class EnvelopeCard : public Card
{
public:
    explicit EnvelopeCard (SimpleSamplerProcessor&);
    void setPad (int padIndex) override;
    void resized() override;

private:
    EnvelopeEditor curve;
    NumberField attackField, decayField, sustainField, releaseField;
    std::unique_ptr<ParamField> attackParam, decayParam, sustainParam, releaseParam;
};
