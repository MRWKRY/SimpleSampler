#include "ControlCards.h"
#include "PastelLookAndFeel.h"

using namespace ParamMath;

namespace
{
    const double notANumber = std::numeric_limits<double>::quiet_NaN();

    double parseNumber (const juce::String& text)
    {
        const auto t = text.trim().replaceCharacter (',', '.');
        if (! t.containsAnyOf ("0123456789"))
            return notANumber;
        return t.retainCharacters ("0123456789.-+").getDoubleValue();
    }

    juce::String msText (double ms)
    {
        if (ms < 1000.0)
            return juce::String (ms, ms < 10.0 ? 1 : 0) + " ms";
        return juce::String (ms / 1000.0, 2) + " s";
    }

    double parseMs (const juce::String& text)
    {
        const double v = parseNumber (text);
        if (std::isnan (v))
            return v;
        const auto lower = text.toLowerCase();
        const bool seconds = lower.containsChar ('s') && ! lower.contains ("ms");
        return seconds ? v * 1000.0 : v;
    }
}

//==============================================================================
Card::Card (SimpleSamplerProcessor& p, const juce::String& t) : processor (p), title (t) {}

void Card::paint (juce::Graphics& g)
{
    PastelLookAndFeel::drawCard (g, getLocalBounds().toFloat().reduced (1.0f));

    auto row = getLocalBounds().reduced (14, 0).withTrimmedTop (9).withHeight (titleHeight - 4);
    g.setColour (PastelLookAndFeel::text());
    g.setFont (PastelLookAndFeel::font (14.0f, true));
    g.drawText (title, row, juce::Justification::centredLeft, true);

    if (summary.isNotEmpty())
    {
        g.setColour (PastelLookAndFeel::textSoft());
        g.setFont (PastelLookAndFeel::font (12.5f));
        g.drawText (summary, row, juce::Justification::centredRight, true);
    }
}

juce::Rectangle<int> Card::contentArea() const
{
    return getLocalBounds().reduced (14, 0).withTrimmedTop (9 + titleHeight).withTrimmedBottom (12);
}

void Card::layoutRow (juce::Rectangle<int> row, std::initializer_list<juce::Component*> items, int gap)
{
    const int n = (int) items.size();
    if (n == 0)
        return;

    const int w = (row.getWidth() - gap * (n - 1)) / n;
    int x = row.getX();
    for (auto* c : items)
    {
        c->setBounds (x, row.getY(), w, row.getHeight());
        x += w + gap;
    }
}

//==============================================================================
BpmCard::BpmCard (SimpleSamplerProcessor& p) : Card (p, "BPM")
{
    startField.setCaption ("Start");
    resultField.setCaption ("Result");
    diffField.setCaption ("Diff");

    for (auto* f : { &startField, &resultField })
    {
        f->setRange (minBpm, maxBpm, 0.01, 1.0);
        f->textFromValue = [] (double v) { return bpmText (v); };
        f->valueFromText = parseNumber;
        addAndMakeVisible (*f);
    }

    diffField.setRange (-(double) (maxBpm - minBpm), (double) (maxBpm - minBpm), 0.01, 1.0);
    diffField.textFromValue = [] (double v) { return signedBpmText (v); };
    diffField.valueFromText = parseNumber;
    diffField.setDefaultValue (0.0);
    addAndMakeVisible (diffField);

    // the diff box edits the result BPM: result = start + diff
    diffField.onGestureStart = [this] { if (resultParam) resultParam->beginGesture(); };
    diffField.onGestureEnd   = [this] { if (resultParam) resultParam->endGesture(); };
    diffField.onValueChange  = [this] (double diff)
    {
        if (startParam && resultParam)
            resultParam->setDuringGesture (resultBpmFromDiff (startParam->get(), diff));
    };

    setPad (0);
}

void BpmCard::setPad (int padIndex)
{
    pad = padIndex;
    startParam.reset();
    resultParam.reset();
    startParam  = std::make_unique<ParamField> (*processor.getPadParameter (pad, id::srcBpm), startField,  [this] { refresh(); });
    resultParam = std::make_unique<ParamField> (*processor.getPadParameter (pad, id::dstBpm), resultField, [this] { refresh(); });
    refresh();
}

void BpmCard::refresh()
{
    if (! startParam || ! resultParam)
        return;

    const double start = startParam->get(), result = resultParam->get();
    const double diff = diffFromResultBpm (start, result);
    diffField.setValue (diff);

    summary = bpmText (start) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + bpmText (result) + " / " + signedBpmText (diff)
            + juce::String::fromUTF8 ("   \xc3\x97") + juce::String (stretchRatio (start, result), 3);
    repaint();
}

void BpmCard::resized()
{
    layoutRow (contentArea(), { &startField, &resultField, &diffField });
}

//==============================================================================
PitchCard::PitchCard (SimpleSamplerProcessor& p) : Card (p, "Pitch")
{
    startField.setCaption ("Start");
    resultField.setCaption ("Result");
    semitoneField.setCaption ("Semitones");

    auto parseNoteValue = [] (const juce::String& t)
    {
        const int n = parseNote (t);
        return n <= -1000 ? notANumber : (double) n;
    };

    startField.setRange (0, 127, 1, 1);
    startField.setPixelsPerStep (6.0);
    startField.textFromValue = [] (double v) { return noteName (juce::roundToInt (v)); };
    startField.valueFromText = parseNoteValue;
    addAndMakeVisible (startField);

    resultField.setRange (-maxSemitones, 127 + maxSemitones, 1, 1);
    resultField.setPixelsPerStep (6.0);
    resultField.textFromValue = [] (double v) { return noteName (juce::roundToInt (v)); };
    resultField.valueFromText = parseNoteValue;
    addAndMakeVisible (resultField);

    semitoneField.setRange (-maxSemitones, maxSemitones, 1, 1);
    semitoneField.setPixelsPerStep (6.0);
    semitoneField.textFromValue = [] (double v) { return signedText (v) + " st"; };
    semitoneField.valueFromText = parseNumber;
    addAndMakeVisible (semitoneField);

    // the result note edits the semitone shift: semitones = result - start (limited to +-24)
    resultField.onGestureStart = [this] { if (semitoneParam) semitoneParam->beginGesture(); };
    resultField.onGestureEnd   = [this] { if (semitoneParam) semitoneParam->endGesture(); refresh(); };
    resultField.onValueChange  = [this] (double note)
    {
        if (startParam && semitoneParam)
        {
            semitoneParam->setDuringGesture (semitonesFromNotes (juce::roundToInt (startParam->get()), juce::roundToInt (note)));
            refresh();
        }
    };

    setPad (0);
}

void PitchCard::setPad (int padIndex)
{
    pad = padIndex;
    startParam.reset();
    semitoneParam.reset();
    startParam    = std::make_unique<ParamField> (*processor.getPadParameter (pad, id::srcPitch),  startField,    [this] { refresh(); });
    semitoneParam = std::make_unique<ParamField> (*processor.getPadParameter (pad, id::semitones), semitoneField, [this] { refresh(); });
    refresh();
}

void PitchCard::refresh()
{
    if (! startParam || ! semitoneParam)
        return;

    const int start = juce::roundToInt (startParam->get());
    const int st = juce::roundToInt (semitoneParam->get());
    const int result = resultNoteFromSemitones (start, st);
    resultField.setValue (result);

    summary = noteName (start) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + noteName (result) + " / " + signedText (st) + " st";
    repaint();
}

void PitchCard::resized()
{
    layoutRow (contentArea(), { &startField, &resultField, &semitoneField });
}

//==============================================================================
LoopCard::LoopCard (SimpleSamplerProcessor& p) : Card (p, "Loop")
{
    addAndMakeVisible (loopToggle);

    startField.setCaption ("Start");
    endField.setCaption ("End");

    for (auto* f : { &startField, &endField })
    {
        f->setRange (0.0, 1.0, 0.0, 0.002);
        f->setPixelsPerStep (2.0);
        f->textFromValue = [this] (double v)
        {
            return lengthSeconds > 0.0 ? juce::String (v * lengthSeconds, 3) + " s"
                                       : juce::String (v * 100.0, 1) + " %";
        };
        f->valueFromText = [this] (const juce::String& t)
        {
            const double v = parseNumber (t);
            if (std::isnan (v))
                return v;
            if (t.containsChar ('%') || lengthSeconds <= 0.0)
                return v / 100.0;
            return v / lengthSeconds;
        };
        addAndMakeVisible (*f);
    }

    setPad (0);
}

void LoopCard::setPad (int padIndex)
{
    pad = padIndex;
    toggleAttachment.reset();
    startParam.reset();
    endParam.reset();

    toggleAttachment = std::make_unique<juce::ButtonParameterAttachment> (*processor.getPadParameter (pad, id::loop), loopToggle);
    startParam = std::make_unique<ParamField> (*processor.getPadParameter (pad, id::loopStart), startField, [this] { refresh(); });
    endParam   = std::make_unique<ParamField> (*processor.getPadParameter (pad, id::loopEnd),   endField,   [this] { refresh(); });

    // keep start < end while editing
    startField.onValueChange = [this] (double v)
    {
        if (startParam && endParam)
            startParam->setDuringGesture (juce::jmin (v, endParam->get() - 0.001));
    };
    endField.onValueChange = [this] (double v)
    {
        if (startParam && endParam)
            endParam->setDuringGesture (juce::jmax (v, startParam->get() + 0.001));
    };

    refresh();
}

void LoopCard::setSampleLength (double seconds)
{
    lengthSeconds = seconds;
    refresh();
}

void LoopCard::refresh()
{
    if (! startParam || ! endParam)
        return;

    const double a = startParam->get(), b = endParam->get();
    summary = lengthSeconds > 0.0 ? juce::String (juce::jmax (0.0, b - a) * lengthSeconds, 3) + " s"
                                  : juce::String();
    startField.repaint();
    endField.repaint();
    repaint();
}

void LoopCard::resized()
{
    auto area = contentArea();
    auto toggleArea = area.removeFromLeft (juce::jmin (96, area.getWidth() / 3));
    loopToggle.setBounds (toggleArea.withTrimmedTop (NumberField::captionHeight));
    area.removeFromLeft (8);
    layoutRow (area, { &startField, &endField });
}

//==============================================================================
EnvelopeCard::EnvelopeCard (SimpleSamplerProcessor& p) : Card (p, "Envelope"), curve (p)
{
    addAndMakeVisible (curve);

    attackField.setCaption ("Attack");
    decayField.setCaption ("Decay");
    sustainField.setCaption ("Sustain");
    releaseField.setCaption ("Release");

    auto setupTime = [this] (NumberField& f, double minMs)
    {
        f.setRange (minMs, maxEnvMs, 0.1, 5.0);
        f.setPixelsPerStep (2.0);
        f.textFromValue = msText;
        f.valueFromText = parseMs;
        addAndMakeVisible (f);
    };
    setupTime (attackField, minAttackMs);
    setupTime (decayField, minDecayMs);
    setupTime (releaseField, minReleaseMs);

    sustainField.setRange (0.0, 1.0, 0.001, 0.01);
    sustainField.setPixelsPerStep (2.0);
    sustainField.textFromValue = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
    sustainField.valueFromText = [] (const juce::String& t) { const double v = parseNumber (t); return std::isnan (v) ? v : v / 100.0; };
    addAndMakeVisible (sustainField);

    setPad (0);
}

void EnvelopeCard::setPad (int padIndex)
{
    pad = padIndex;
    attackParam.reset(); decayParam.reset(); sustainParam.reset(); releaseParam.reset();
    attackParam  = std::make_unique<ParamField> (*processor.getPadParameter (pad, id::attack),  attackField);
    decayParam   = std::make_unique<ParamField> (*processor.getPadParameter (pad, id::decay),   decayField);
    sustainParam = std::make_unique<ParamField> (*processor.getPadParameter (pad, id::sustain), sustainField);
    releaseParam = std::make_unique<ParamField> (*processor.getPadParameter (pad, id::release), releaseField);
    curve.setPad (pad);
}

void EnvelopeCard::resized()
{
    auto area = contentArea();
    const int fieldsHeight = NumberField::captionHeight + 28;
    auto fields = area.removeFromBottom (juce::jmin (fieldsHeight, area.getHeight() / 2));
    area.removeFromBottom (6);
    curve.setBounds (area);
    layoutRow (fields, { &attackField, &decayField, &sustainField, &releaseField }, 6);
}
