#include "ModPanel.h"

using namespace ParamMath;

namespace
{
    constexpr int cardGap = 10;
}

//==============================================================================
ModDisplay::ModDisplay (SimpleSamplerProcessor& p) : processor (p)
{
    setMod (0);
    startTimerHz (30);
}

ModDisplay::~ModDisplay()
{
    stopTimer();
}

void ModDisplay::setMod (int modIndex)
{
    mod = juce::jlimit (0, numMods - 1, modIndex);
    typeParam = processor.apvts.getParameter (id::mod (mod, id::modType));
    for (int s = 0; s < TempoSync::numSteps; ++s)
        stepParams[s] = processor.apvts.getParameter (id::modStep (mod, s));
    lastShape = -1;
    repaint();
}

int ModDisplay::currentShape() const
{
    return typeParam != nullptr ? juce::roundToInt (typeParam->convertFrom0to1 (typeParam->getValue())) : 0;
}

void ModDisplay::timerCallback()
{
    if (! isShowing())
        return;

    bool changed = false;
    const int shape = currentShape();
    if (shape != lastShape) { lastShape = shape; changed = true; }

    for (int s = 0; s < TempoSync::numSteps; ++s)
    {
        const float v = stepParams[s] != nullptr ? stepParams[s]->convertFrom0to1 (stepParams[s]->getValue()) : 0.0f;
        if (std::abs (v - lastSteps[s]) > 1.0e-4f) { lastSteps[s] = v; changed = true; }
    }

    const float position = processor.getModulator (mod).uiUnits.load (std::memory_order_relaxed);
    if (std::abs (position - lastPosition) > 0.002f) { lastPosition = position; changed = true; }

    if (changed)
        repaint();
}

void ModDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (PastelLookAndFeel::track());
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    const auto area = bounds.reduced (8.0f, 7.0f);
    const float mid = area.getCentreY(), half = area.getHeight() * 0.5f;
    const auto colour = PastelLookAndFeel::modColour (mod);
    const int shape = currentShape();

    g.setColour (PastelLookAndFeel::track());
    g.fillRect (juce::Rectangle<float> (area.getX(), mid - 0.5f, area.getWidth(), 1.0f));

    if (shape == TempoSync::stepSeq)
    {
        // 16 bars drawn from the centre line; the step being played is highlighted
        const float w = area.getWidth() / (float) TempoSync::numSteps;
        const int playing = juce::jlimit (0, TempoSync::numSteps - 1, (int) (lastPosition * (float) TempoSync::numSteps));
        for (int s = 0; s < TempoSync::numSteps; ++s)
        {
            const float v = lastSteps[s];
            const float h = juce::jmax (1.5f, std::abs (v) * half);
            const juce::Rectangle<float> bar (area.getX() + w * (float) s + 1.0f, v >= 0.0f ? mid - h : mid, w - 2.0f, h);
            g.setColour (colour.withAlpha (s == playing ? 1.0f : (s % 4 == 0 ? 0.62f : 0.42f)));
            g.fillRoundedRectangle (bar, 2.0f);
        }
        return;
    }

    // wave preview: one cycle for the LFO shapes, a stretch of eight steps for the random ones
    const bool random = shape == TempoSync::sampleHold || shape == TempoSync::smoothRandom;
    const double span = random ? 8.0 : 1.0;
    const auto seed = (juce::uint32) (mod + 1) * 7919u;

    juce::Path wave;
    const int points = juce::jmax (2, (int) area.getWidth());
    for (int i = 0; i < points; ++i)
    {
        const double units = span * (double) i / (double) (points - 1) * 0.9999;
        const float v = TempoSync::shapeValue (shape, units, nullptr, seed);
        const juce::Point<float> pt (area.getX() + (float) i, mid - v * half);
        if (i == 0) wave.startNewSubPath (pt); else wave.lineTo (pt);
    }
    g.setColour (colour);
    g.strokePath (wave, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // where the modulator is right now
    const float value = processor.getModulator (mod).uiValue.load (std::memory_order_relaxed);
    const float x = random ? area.getRight() - 4.0f : area.getX() + lastPosition * area.getWidth();
    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ x, mid - value * half }));
    g.setColour (colour);
    g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ x, mid - value * half }), 1.8f);
}

void ModDisplay::editStep (const juce::MouseEvent& e)
{
    if (currentShape() != TempoSync::stepSeq)
        return;

    const auto area = getLocalBounds().toFloat().reduced (8.0f, 7.0f);
    const int step = juce::jlimit (0, TempoSync::numSteps - 1, (int) ((e.position.x - area.getX()) / area.getWidth() * (float) TempoSync::numSteps));
    float value = juce::jlimit (-1.0f, 1.0f, (area.getCentreY() - e.position.y) / (area.getHeight() * 0.5f));
    if (std::abs (value) < 0.06f)
        value = 0.0f;     // easy to hit the centre

    if (auto* param = stepParams[step])
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (value));
        param->endChangeGesture();
    }
}

void ModDisplay::mouseDown (const juce::MouseEvent& e)   { editStep (e); }
void ModDisplay::mouseDrag (const juce::MouseEvent& e)   { editStep (e); }

//==============================================================================
ModulatorCard::ModulatorCard (SimpleSamplerProcessor& p) : Card (p, "Modulator 1"), display (p)
{
    for (auto* c : { &type, &rate, &mode })
        addAndMakeVisible (*c);
    hz.setCaption ("Rate (free)");
    addChildComponent (hz);
    addAndMakeVisible (syncToggle);
    addAndMakeVisible (display);
    setMod (0);
}

void ModulatorCard::setMod (int modIndex)
{
    mod = juce::jlimit (0, numMods - 1, modIndex);
    title = "Modulator " + juce::String (mod + 1);
    summary = "drag M" + juce::String (mod + 1) + " onto a knob";

    auto& apvts = processor.apvts;
    type.bind (apvts.getParameter (id::mod (mod, id::modType)));
    rate.bind (apvts.getParameter (id::mod (mod, id::modRate)));
    mode.bind (apvts.getParameter (id::mod (mod, id::modMode)));

    hzParam.reset();
    hzParam = attachField (hz, apvts.getParameter (id::mod (mod, id::modHz)), 0.05);

    syncAttachment.reset();
    syncListener.reset();
    auto* syncParam = apvts.getParameter (id::mod (mod, id::modSync));
    syncAttachment = std::make_unique<juce::ButtonParameterAttachment> (*syncParam, syncToggle);
    syncListener = std::make_unique<juce::ParameterAttachment> (*syncParam, [this] (float v) { syncChanged (v > 0.5f); });
    syncListener->sendInitialUpdate();

    display.setMod (mod);
    repaint();
}

void ModulatorCard::syncChanged (bool isSynced)
{
    rate.setVisible (isSynced);
    hz.setVisible (! isSynced);
}

void ModulatorCard::resized()
{
    auto area = contentArea();
    auto row = area.removeFromTop (fieldRowHeight);
    const int gap = 6;
    const int toggleWidth = 70;
    const int w = (row.getWidth() - toggleWidth - gap * 3) / 3;

    type.setBounds (row.removeFromLeft (w));
    row.removeFromLeft (gap);
    const auto rateArea = row.removeFromLeft (w);
    rate.setBounds (rateArea);
    hz.setBounds (rateArea);
    row.removeFromLeft (gap);
    mode.setBounds (row.removeFromLeft (w));
    row.removeFromLeft (gap);
    syncToggle.setBounds (row.withTrimmedTop (NumberField::captionHeight));

    area.removeFromTop (6);
    display.setBounds (area.removeFromTop (76));
}

//==============================================================================
MatrixCard::Row::Row (SimpleSamplerProcessor& p, const ModMatrix::SlotInfo& slot) : info (slot), processor (p)
{
    depth.setRange (-100.0, 100.0, 1.0, 1.0);
    depth.setPixelsPerStep (2.0);
    depth.setDefaultValue (50.0);
    depth.textFromValue = [] (double v) { return signedText (v) + " %"; };
    depth.onValueChange = [this] (double v) { processor.getModMatrix().setDepth (info.slot, (float) v * 0.01f); };
    addAndMakeVisible (depth);

    remove.setTooltip ("Remove this assignment");
    remove.onClick = [this] { processor.getModMatrix().removeSlot (info.slot); };
    addAndMakeVisible (remove);

    update (slot);
}

void MatrixCard::Row::update (const ModMatrix::SlotInfo& slot)
{
    info = slot;
    targetName = processor.getModMatrix().getTarget (slot.target).name;
    depth.setValue (std::round (slot.depth * 100.0f));
    repaint();
}

void MatrixCard::Row::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().reduced (0, 2);
    const auto colour = PastelLookAndFeel::modColour (info.source);

    auto chip = area.removeFromLeft (30).toFloat().reduced (0.0f, 2.0f);
    g.setColour (colour.withAlpha (0.16f));
    g.fillRoundedRectangle (chip, 7.0f);
    g.setColour (colour.darker (0.25f));
    g.setFont (PastelLookAndFeel::font (11.0f, true));
    g.drawText ("M" + juce::String (info.source + 1), chip, juce::Justification::centred, false);

    area.removeFromLeft (8);
    area.setRight (depth.getX() - 6);
    g.setColour (PastelLookAndFeel::text());
    g.setFont (PastelLookAndFeel::font (12.5f));
    g.drawFittedText (targetName, area, juce::Justification::centredLeft, 1, 0.8f);
}

void MatrixCard::Row::resized()
{
    auto area = getLocalBounds().reduced (0, 2);
    remove.setBounds (area.removeFromRight (24));
    area.removeFromRight (5);
    depth.setBounds (area.removeFromRight (66));
}

MatrixCard::MatrixCard (SimpleSamplerProcessor& p) : Card (p, "Assignments")
{
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
    refresh();
}

void MatrixCard::refresh()
{
    auto& matrix = processor.getModMatrix();
    const auto version = matrix.version.load();
    if (version == matrixVersion)
        return;
    matrixVersion = version;

    const auto slots = matrix.getSlots();

    // same assignments as before (only depths changed)? then just update the rows in place,
    // so a depth box that is being dragged is not destroyed under the mouse
    bool same = (int) slots.size() == rows.size();
    for (size_t i = 0; same && i < slots.size(); ++i)
        same = rows[(int) i]->info.slot == slots[i].slot && rows[(int) i]->info.source == slots[i].source
               && rows[(int) i]->info.target == slots[i].target;

    if (same)
    {
        for (size_t i = 0; i < slots.size(); ++i)
            rows[(int) i]->update (slots[i]);
    }
    else
    {
        rows.clear();
        for (const auto& s : slots)
            list.addAndMakeVisible (rows.add (new Row (processor, s)));
        resized();
    }

    summary = juce::String (slots.size()) + " / " + juce::String (ModMatrix::maxSlots);
    repaint();
}

void MatrixCard::resized()
{
    viewport.setBounds (contentArea());
    const int width = viewport.getWidth() - (rows.size() * rowHeight > viewport.getHeight() ? viewport.getScrollBarThickness() + 2 : 0);
    list.setSize (juce::jmax (10, width), juce::jmax (1, rows.size() * rowHeight));
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowHeight, list.getWidth(), rowHeight);
}

void MatrixCard::paint (juce::Graphics& g)
{
    Card::paint (g);
    if (rows.isEmpty())
    {
        g.setColour (PastelLookAndFeel::textSoft());
        g.setFont (PastelLookAndFeel::font (12.5f));
        g.drawFittedText ("Nothing assigned yet.\nDrag one of the M1 - M4 chips (top right) onto any knob\nin the Tone/FX or Genre tab.",
                          contentArea(), juce::Justification::centred, 3);
    }
}

//==============================================================================
RandomizerCard::RandomizerCard (SimpleSamplerProcessor& p) : Card (p, "Randomize")
{
    addAndMakeVisible (randomizeButton);
    addAndMakeVisible (undoButton);
    addAndMakeVisible (autoToggle);
    addAndMakeVisible (scope);
    addAndMakeVisible (interval);

    randomizeButton.setTooltip ("Randomize the unlocked knobs (right-click a knob to lock it)");
    randomizeButton.onClick = [this]
    {
        processor.getRandomizer().randomize (processor.getSelectedPad());
        refresh();
    };
    undoButton.onClick = [this]
    {
        processor.getRandomizer().undo();
        refresh();
    };
    autoToggle.setTooltip ("Re-randomize in time with the tempo (temporary offsets: the knobs themselves stay where they are)");

    auto& apvts = p.apvts;
    scope.bind (apvts.getParameter (id::rndScope));
    interval.bind (apvts.getParameter (id::rndInterval));
    autoAttachment = std::make_unique<juce::ButtonParameterAttachment> (*apvts.getParameter (id::rndAuto), autoToggle);

    amount.setCaption ("Amount");
    tempo.setCaption ("Tempo (no host)");
    addAndMakeVisible (amount);
    addAndMakeVisible (tempo);
    amountParam = attachField (amount, apvts.getParameter (id::rndAmount), 1.0);
    tempoParam  = attachField (tempo,  apvts.getParameter (id::tempo), 1.0);

    refresh();
}

void RandomizerCard::refresh()
{
    undoButton.setEnabled (processor.getRandomizer().canUndo());

    const auto text = juce::String (processor.getCurrentBpm(), 1) + " BPM (" + (processor.isUsingHostTempo() ? "host" : "manual") + ")";
    if (text != lastSummary)
    {
        lastSummary = text;
        summary = text;
        repaint();
    }
}

void RandomizerCard::resized()
{
    auto area = contentArea();
    auto row = area.removeFromTop (28);
    randomizeButton.setBounds (row.removeFromLeft (112));
    row.removeFromLeft (6);
    undoButton.setBounds (row.removeFromLeft (66));
    autoToggle.setBounds (row.removeFromRight (76));
    area.removeFromTop (6);
    layoutRow (area.removeFromTop (fieldRowHeight), { &scope, &amount, &interval, &tempo }, 6);
}

//==============================================================================
ModPanel::ModPanel (SimpleSamplerProcessor& p) : modulator (p), matrix (p), randomizer (p)
{
    addAndMakeVisible (modulator);
    addAndMakeVisible (matrix);
    addAndMakeVisible (randomizer);
    startTimerHz (12);
}

ModPanel::~ModPanel()
{
    stopTimer();
}

void ModPanel::setMod (int modIndex)
{
    mod = juce::jlimit (0, numMods - 1, modIndex);
    modulator.setMod (mod);
}

void ModPanel::timerCallback()
{
    if (! isShowing())
        return;
    matrix.refresh();
    randomizer.refresh();
}

void ModPanel::resized()
{
    auto area = getLocalBounds();
    modulator.setBounds (area.removeFromTop (ModulatorCard::height));
    area.removeFromTop (cardGap);
    randomizer.setBounds (area.removeFromBottom (RandomizerCard::height));
    area.removeFromBottom (cardGap);
    matrix.setBounds (area);
}
