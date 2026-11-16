#include "FxPanels.h"

using namespace ParamMath;

namespace
{
    juce::String padText (int pad)   { return "Pad " + juce::String (pad + 1).paddedLeft ('0', 2); }
    constexpr int cardGap = 10;
}

//==============================================================================
PadToneCard::PadToneCard (SimpleSamplerProcessor& p)
    : Card (p, "Pad Tone"),
      level (p, "Level"), pan (p, "Pan"), drive (p, "Drive"), sendReverb (p, "Reverb"), sendDelay (p, "Delay"),
      eqLow (p, "EQ Low"), eqMid (p, "EQ Mid"), eqFreq (p, "Mid Freq"), eqHigh (p, "EQ High")
{
    for (auto* k : { &level, &pan, &drive, &sendReverb, &sendDelay, &eqLow, &eqMid, &eqFreq, &eqHigh })
        addAndMakeVisible (*k);
    setPad (0);
}

void PadToneCard::setPad (int padIndex)
{
    pad = padIndex;
    summary = padText (pad);
    level.setParameter (id::pad (pad, id::level));
    pan.setParameter (id::pad (pad, id::pan));
    drive.setParameter (id::pad (pad, id::drive));
    sendReverb.setParameter (id::pad (pad, id::sendRev));
    sendDelay.setParameter (id::pad (pad, id::sendDly));
    eqLow.setParameter (id::pad (pad, id::eqLow));
    eqMid.setParameter (id::pad (pad, id::eqMid));
    eqFreq.setParameter (id::pad (pad, id::eqMidFreq));
    eqHigh.setParameter (id::pad (pad, id::eqHigh));
    repaint();
}

void PadToneCard::resized()
{
    auto area = contentArea();
    layoutKnobRow (area.removeFromTop (Knob::preferredHeight), { &level, &pan, &drive, &sendReverb, &sendDelay });
    area.removeFromTop (4);
    layoutKnobRow (area.removeFromTop (Knob::preferredHeight), { &eqLow, &eqMid, &eqFreq, &eqHigh });
}

//==============================================================================
ReverbCard::ReverbCard (SimpleSamplerProcessor& p)
    : Card (p, "Reverb"),
      size (p, "Size"), decay (p, "Decay"), preDelay (p, "Pre-delay"), lowCut (p, "Low Cut"), highCut (p, "High Cut"), mix (p, "Mix")
{
    summary = "shared by all pads";
    for (auto* k : { &size, &decay, &preDelay, &lowCut, &highCut, &mix })
        addAndMakeVisible (*k);

    size.setParameter (id::rvSize);
    decay.setParameter (id::rvDecay);
    preDelay.setParameter (id::rvPreDelay);
    lowCut.setParameter (id::rvLowCut);
    highCut.setParameter (id::rvHighCut);
    mix.setParameter (id::rvMix);
}

void ReverbCard::resized()
{
    layoutKnobRow (contentArea().removeFromTop (Knob::preferredHeight), { &size, &decay, &preDelay, &lowCut, &highCut, &mix });
}

//==============================================================================
DelayCard::DelayCard (SimpleSamplerProcessor& p)
    : Card (p, "Delay"),
      time (p, "Time"), feedback (p, "Feedback"), lowCut (p, "Low Cut"), highCut (p, "High Cut"), mix (p, "Mix")
{
    summary = "shared by all pads";
    for (auto* k : { &time, &feedback, &lowCut, &highCut, &mix })
        addAndMakeVisible (*k);
    addAndMakeVisible (syncToggle);
    addAndMakeVisible (pingPongToggle);
    addAndMakeVisible (division);

    time.setParameter (id::dlTime);
    feedback.setParameter (id::dlFeedback);
    lowCut.setParameter (id::dlLowCut);
    highCut.setParameter (id::dlHighCut);
    mix.setParameter (id::dlMix);
    division.bind (p.apvts.getParameter (id::dlDivision));

    auto* syncParam = p.apvts.getParameter (id::dlSync);
    syncAttachment = std::make_unique<juce::ButtonParameterAttachment> (*syncParam, syncToggle);
    pingPongAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p.apvts.getParameter (id::dlPingPong), pingPongToggle);
    syncListener = std::make_unique<juce::ParameterAttachment> (*syncParam, [this] (float v) { syncChanged (v > 0.5f); });
    syncListener->sendInitialUpdate();
}

void DelayCard::syncChanged (bool isSynced)
{
    // synced: the note value sets the time; free: the Time knob (in ms) does
    division.setEnabled (isSynced);
    time.setEnabled (! isSynced);
}

void DelayCard::resized()
{
    auto area = contentArea();
    auto row = area.removeFromTop (fieldRowHeight);
    syncToggle.setBounds (row.removeFromLeft (78).withTrimmedTop (NumberField::captionHeight));
    row.removeFromLeft (4);
    division.setBounds (row.removeFromLeft (96));
    pingPongToggle.setBounds (row.removeFromRight (112).withTrimmedTop (NumberField::captionHeight));
    area.removeFromTop (6);
    layoutKnobRow (area.removeFromTop (Knob::preferredHeight), { &time, &feedback, &lowCut, &highCut, &mix });
}

//==============================================================================
TonePanel::TonePanel (SimpleSamplerProcessor& p) : tone (p), reverb (p), delay (p)
{
    addAndMakeVisible (tone);
    addAndMakeVisible (reverb);
    addAndMakeVisible (delay);
}

void TonePanel::setPad (int padIndex)   { tone.setPad (padIndex); }

void TonePanel::resized()
{
    auto area = getLocalBounds();
    tone.setBounds (area.removeFromTop (PadToneCard::height));
    area.removeFromTop (cardGap);
    reverb.setBounds (area.removeFromTop (ReverbCard::height));
    area.removeFromTop (cardGap);
    delay.setBounds (area.removeFromTop (DelayCard::height));
}

//==============================================================================
BotanicaCard::BotanicaCard (SimpleSamplerProcessor& p)
    : Card (p, "Botanica"),
      bloom (p, "Bloom"), grain (p, "Grain"), drift (p, "Drift"), warmth (p, "Warmth"), dew (p, "Dew")
{
    for (auto* k : { &bloom, &grain, &drift, &warmth, &dew })
        addAndMakeVisible (*k);
    setPad (0);
}

void BotanicaCard::setPad (int padIndex)
{
    pad = padIndex;
    summary = padText (pad);
    bloom.setParameter (id::pad (pad, id::bloom));
    grain.setParameter (id::pad (pad, id::grain));
    drift.setParameter (id::pad (pad, id::drift));
    warmth.setParameter (id::pad (pad, id::warmth));
    dew.setParameter (id::pad (pad, id::dew));
    repaint();
}

void BotanicaCard::resized()
{
    layoutKnobRow (contentArea().removeFromTop (Knob::preferredHeight), { &bloom, &grain, &drift, &warmth, &dew });
}

//==============================================================================
FutureBassCard::StepToggle::StepToggle (juce::RangedAudioParameter& p, int stepIndex)
    : param (p), index (stepIndex),
      attachment (p, [this] (float v) { on = v > 0.5f; repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void FutureBassCard::StepToggle::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (1.5f, 2.0f);
    const bool beat = index % 4 == 0;
    g.setColour (on ? PastelLookAndFeel::accent().withAlpha (beat ? 1.0f : 0.72f)
                    : juce::Colours::white.withAlpha (0.8f));
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (on ? PastelLookAndFeel::accent() : (beat ? PastelLookAndFeel::textSoft().withAlpha (0.7f) : PastelLookAndFeel::track()));
    g.drawRoundedRectangle (b, 4.0f, 1.0f);
}

void FutureBassCard::StepToggle::mouseDown (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

namespace
{
    struct GatePattern { const char* name; bool steps[TempoSync::numSteps]; };
    const GatePattern gatePatterns[] =
    {
        { "Trance A",   { 1,0,1,1, 1,0,1,1, 1,0,1,0, 1,1,1,0 } },
        { "Trance B",   { 1,1,0,1, 1,0,1,1, 0,1,1,0, 1,1,0,1 } },
        { "Gallop",     { 1,0,1,1, 1,0,1,1, 1,0,1,1, 1,0,1,1 } },
        { "Pulse 1/8",  { 1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0 } },
        { "Pulse 1/4",  { 1,1,0,0, 1,1,0,0, 1,1,0,0, 1,1,0,0 } },
        { "Offbeat",    { 0,0,1,1, 0,0,1,1, 0,0,1,1, 0,0,1,1 } },
        { "Stutter",    { 1,1,1,1, 0,0,0,0, 1,0,1,0, 1,1,0,0 } },
        { "Sparse",     { 1,0,0,0, 0,0,1,0, 0,0,1,0, 0,0,0,0 } },
        { "All on",     { 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1 } }
    };
}

FutureBassCard::FutureBassCard (SimpleSamplerProcessor& p)
    : Card (p, "Future Bass"),
      pump (p, "Pump"), wobble (p, "Wobble"), gate (p, "Gate"), width (p, "Width"),
      crush (p, "Crush"), dive (p, "Dive"), formant (p, "Formant")
{
    for (auto* k : { &pump, &wobble, &gate, &width, &crush, &dive, &formant })
        addAndMakeVisible (*k);

    for (auto* c : { &pumpRate, &wobbleRate, &gateRate, &pattern })
        addAndMakeVisible (*c);
    pumpRate.bind (p.apvts.getParameter (id::pumpRate));
    wobbleRate.bind (p.apvts.getParameter (id::wobbleRate));
    gateRate.bind (p.apvts.getParameter (id::gateRate));

    // pattern presets simply write the 16 step parameters
    int itemId = 1;
    for (const auto& gp : gatePatterns)
        pattern.box.addItem (gp.name, itemId++);
    pattern.box.setTextWhenNothingSelected ("Choose...");
    pattern.box.onChange = [this]
    {
        const int index = pattern.box.getSelectedId() - 1;
        if (index >= 0)
        {
            applyPattern (index);
            pattern.box.setSelectedId (0, juce::dontSendNotification);   // the steps are the truth, not the menu
        }
    };

    pumpRelease.setCaption ("Pump release");
    diveRange.setCaption ("Dive range");
    diveTime.setCaption ("Dive time");
    formantRange.setCaption ("Formant range");
    for (auto* f : { &pumpRelease, &diveRange, &diveTime, &formantRange })
        addAndMakeVisible (*f);
    pumpReleaseParam  = attachField (pumpRelease,  p.apvts.getParameter (id::pumpRelease), 1.0);
    diveRangeParam    = attachField (diveRange,    p.apvts.getParameter (id::diveRange), 1.0);
    diveTimeParam     = attachField (diveTime,     p.apvts.getParameter (id::diveTime), 10.0);
    formantRangeParam = attachField (formantRange, p.apvts.getParameter (id::formantRange), 0.5);

    for (int s = 0; s < TempoSync::numSteps; ++s)
        addAndMakeVisible (steps.add (new StepToggle (*p.apvts.getParameter (id::gateStep (s)), s)));

    setPad (0);
}

void FutureBassCard::applyPattern (int patternIndex)
{
    if (patternIndex < 0 || patternIndex >= (int) std::size (gatePatterns))
        return;

    for (int s = 0; s < TempoSync::numSteps; ++s)
    {
        if (auto* param = processor.apvts.getParameter (id::gateStep (s)))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (gatePatterns[patternIndex].steps[s] ? 1.0f : 0.0f);
            param->endChangeGesture();
        }
    }
}

void FutureBassCard::setPad (int padIndex)
{
    pad = padIndex;
    summary = padText (pad);
    pump.setParameter (id::pad (pad, id::pump));
    wobble.setParameter (id::pad (pad, id::wobble));
    gate.setParameter (id::pad (pad, id::gate));
    width.setParameter (id::pad (pad, id::width));
    crush.setParameter (id::pad (pad, id::crush));
    dive.setParameter (id::pad (pad, id::dive));
    formant.setParameter (id::pad (pad, id::formant));
    repaint();
}

void FutureBassCard::resized()
{
    auto area = contentArea();
    layoutKnobRow (area.removeFromTop (Knob::preferredHeight), { &pump, &wobble, &gate, &width, &crush, &dive, &formant });
    area.removeFromTop (6);

    layoutRow (area.removeFromTop (fieldRowHeight), { &pumpRate, &pumpRelease, &wobbleRate });
    area.removeFromTop (4);
    layoutRow (area.removeFromTop (fieldRowHeight), { &gateRate, &pattern });
    area.removeFromTop (4);

    auto stepRow = area.removeFromTop (stepRowHeight);
    const float w = (float) stepRow.getWidth() / (float) TempoSync::numSteps;
    for (int s = 0; s < steps.size(); ++s)
        steps[s]->setBounds (juce::Rectangle<float> ((float) stepRow.getX() + w * (float) s, (float) stepRow.getY(), w, (float) stepRow.getHeight()).toNearestInt());
    area.removeFromTop (6);

    layoutRow (area.removeFromTop (fieldRowHeight), { &diveRange, &diveTime, &formantRange });
}

//==============================================================================
GenrePanel::GenrePanel (SimpleSamplerProcessor& p) : botanica (p), futureBass (p)
{
    addAndMakeVisible (botanica);
    addAndMakeVisible (futureBass);
}

void GenrePanel::setPad (int padIndex)
{
    botanica.setPad (padIndex);
    futureBass.setPad (padIndex);
}

void GenrePanel::resized()
{
    auto area = getLocalBounds();
    botanica.setBounds (area.removeFromTop (BotanicaCard::height));
    area.removeFromTop (cardGap);
    futureBass.setBounds (area.removeFromTop (FutureBassCard::height));
}
