#pragma once

#include "../ControlCards.h"
#include "Knob.h"
#include "Widgets.h"

//==============================================================================
// [Tone/FX] tab
//==============================================================================
class PadToneCard : public Card
{
public:
    explicit PadToneCard (SimpleSamplerProcessor&);
    void setPad (int padIndex) override;
    void resized() override;
    static constexpr int height = 33 + Knob::preferredHeight * 2 + 4 + 12;

private:
    Knob level, pan, drive, sendReverb, sendDelay;
    Knob eqLow, eqMid, eqFreq, eqHigh;
};

class ReverbCard : public Card
{
public:
    explicit ReverbCard (SimpleSamplerProcessor&);
    void setPad (int) override {}
    void resized() override;
    static constexpr int height = 33 + Knob::preferredHeight + 12;

private:
    Knob size, decay, preDelay, lowCut, highCut, mix;
};

class DelayCard : public Card
{
public:
    explicit DelayCard (SimpleSamplerProcessor&);
    void setPad (int) override {}
    void resized() override;
    static constexpr int height = 33 + fieldRowHeight + 6 + Knob::preferredHeight + 12;

private:
    void syncChanged (bool isSynced);

    juce::ToggleButton syncToggle { "Sync" }, pingPongToggle { "Ping-pong" };
    CaptionCombo division { "Time (sync)" };
    Knob time, feedback, lowCut, highCut, mix;
    std::unique_ptr<juce::ButtonParameterAttachment> syncAttachment, pingPongAttachment;
    std::unique_ptr<juce::ParameterAttachment> syncListener;
};

class TonePanel : public juce::Component
{
public:
    explicit TonePanel (SimpleSamplerProcessor&);
    void setPad (int padIndex);
    void resized() override;

private:
    PadToneCard tone;
    ReverbCard reverb;
    DelayCard delay;
};

//==============================================================================
// [Genre] tab
//==============================================================================
class BotanicaCard : public Card
{
public:
    explicit BotanicaCard (SimpleSamplerProcessor&);
    void setPad (int padIndex) override;
    void resized() override;
    static constexpr int height = 33 + Knob::preferredHeight + 12;

private:
    Knob bloom, grain, drift, warmth, dew;
};

class FutureBassCard : public Card
{
public:
    explicit FutureBassCard (SimpleSamplerProcessor&);
    void setPad (int padIndex) override;
    void resized() override;
    static constexpr int stepRowHeight = 24;
    static constexpr int height = 33 + Knob::preferredHeight + 6 + (fieldRowHeight + 4) * 3 + stepRowHeight + 6 + 12;

private:
    /** One step of the gate pattern (a bool parameter). */
    class StepToggle : public juce::Component
    {
    public:
        StepToggle (juce::RangedAudioParameter& p, int stepIndex);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
    private:
        juce::RangedAudioParameter& param;
        const int index;
        bool on = false;
        juce::ParameterAttachment attachment;
    };

    void applyPattern (int patternIndex);

    Knob pump, wobble, gate, width, crush, dive, formant;
    CaptionCombo pumpRate { "Pump rate" }, wobbleRate { "Wobble rate" }, gateRate { "Gate step" }, pattern { "Gate pattern" };
    NumberField pumpRelease, diveRange, diveTime, formantRange;
    std::unique_ptr<ParamField> pumpReleaseParam, diveRangeParam, diveTimeParam, formantRangeParam;
    juce::OwnedArray<StepToggle> steps;
};

class GenrePanel : public juce::Component
{
public:
    explicit GenrePanel (SimpleSamplerProcessor&);
    void setPad (int padIndex);
    void resized() override;

private:
    BotanicaCard botanica;
    FutureBassCard futureBass;
};
