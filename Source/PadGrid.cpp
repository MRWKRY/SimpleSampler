#include "PadGrid.h"
#include "PastelLookAndFeel.h"

using namespace ParamMath;

PadButton::PadButton (PadGrid& owner, SimpleSamplerProcessor& p, int padIndex)
    : grid (owner), processor (p), pad (padIndex)
{
    lastTrigger = processor.getPad (pad).triggerCount.load();
    refreshContent();
}

void PadButton::refreshContent()
{
    const auto& slot = processor.getPad (pad);
    const auto sample = slot.getSample();
    hasSample = sample != nullptr;
    hasFile   = slot.hasFile();
    missing   = slot.isMissing();
    loading   = slot.loading.load();
    name      = slot.getFileName().upToLastOccurrenceOf (".", false, false);
    if (name.isEmpty())
        name = slot.getFileName();
    repaint();
}

void PadButton::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

bool PadButton::updateGlow()
{
    const auto& slot = processor.getPad (pad);
    const auto trigger = slot.triggerCount.load (std::memory_order_relaxed);
    const bool playing = slot.activeVoices.load (std::memory_order_relaxed) > 0;
    const int note = slot.getNote();

    float target = glow;
    if (trigger != lastTrigger)
    {
        lastTrigger = trigger;
        target = 1.0f;                                  // flash on every hit
    }
    else
    {
        const float floor = playing ? 0.42f : 0.0f;     // stays softly lit while sounding
        target = floor + (glow - floor) * 0.88f;
        if (std::abs (target - floor) < 0.004f)
            target = floor;
    }

    const bool changed = std::abs (target - glow) > 0.0005f || note != midiNote;
    glow = target;
    midiNote = note;
    if (changed)
        repaint();
    return changed;
}

void PadButton::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (3.0f);
    const float radius = 12.0f;
    const auto colours = PastelLookAndFeel::padColours (pad);
    const bool hover = isMouseOver();

    // glow halo behind the pad
    if (glow > 0.01f)
    {
        g.setColour (colours.a.interpolatedWith (colours.b, 0.5f).withAlpha (0.30f * glow));
        g.fillRoundedRectangle (bounds.expanded (2.5f), radius + 2.5f);
    }

    // body
    g.setColour (juce::Colours::white.withAlpha (hasFile ? 0.92f : (hover ? 0.80f : 0.58f)));
    g.fillRoundedRectangle (bounds, radius);

    if (hasFile)
    {
        const float strength = (missing ? 0.10f : 0.24f) + 0.62f * glow + (hover ? 0.06f : 0.0f) + (pressed ? 0.10f : 0.0f);
        g.setGradientFill (juce::ColourGradient (colours.a.withAlpha (juce::jmin (1.0f, strength)), bounds.getX(), bounds.getY(),
                                                 colours.b.withAlpha (juce::jmin (1.0f, strength)), bounds.getRight(), bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, radius);
    }

    // bright flash on top while the glow is strong
    if (glow > 0.45f)
    {
        g.setColour (juce::Colours::white.withAlpha ((glow - 0.45f) * 0.55f));
        g.fillRoundedRectangle (bounds, radius);
    }

    // outline
    if (selected)
    {
        g.setColour (PastelLookAndFeel::accent());
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 2.2f);
    }
    else
    {
        g.setColour (hasFile ? colours.a.interpolatedWith (colours.b, 0.5f).withAlpha (0.35f)
                             : juce::Colours::white.withAlpha (0.95f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
    }

    if (dragOver)
    {
        g.setColour (PastelLookAndFeel::accent().withAlpha (0.16f));
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (PastelLookAndFeel::accent());
        g.drawRoundedRectangle (bounds.reduced (1.0f), radius, 2.0f);
    }

    // text
    auto textArea = getLocalBounds().reduced (11, 8);
    auto top = textArea.removeFromTop (14);
    g.setFont (PastelLookAndFeel::font (11.0f, true));
    g.setColour (hasFile ? PastelLookAndFeel::text().withAlpha (0.75f) : PastelLookAndFeel::textSoft());
    g.drawText (juce::String (pad + 1).paddedLeft ('0', 2), top, juce::Justification::centredLeft, false);
    g.setFont (PastelLookAndFeel::font (11.0f));
    g.setColour (PastelLookAndFeel::textSoft().withAlpha (hasFile ? 1.0f : 0.8f));
    g.drawText (noteName (midiNote), top, juce::Justification::centredRight, false);

    g.setFont (PastelLookAndFeel::font (12.5f, hasFile));
    if (hasFile)
    {
        g.setColour (missing ? juce::Colour (0xffd9566f) : PastelLookAndFeel::text());
        const auto label = loading ? juce::String ("Loading...") : (missing ? "? " + name : name);
        g.drawFittedText (label, textArea, juce::Justification::centredLeft, 2, 0.85f);
    }
    else
    {
        g.setColour (PastelLookAndFeel::textSoft().withAlpha (hover ? 1.0f : 0.55f));
        g.drawText (hover ? "+ add sample" : "+", textArea, juce::Justification::centredLeft, true);
    }
}

void PadButton::mouseDown (const juce::MouseEvent& e)
{
    if (grid.onSelect)
        grid.onSelect (pad);

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addItem (1, hasFile ? "Replace sample..." : "Load sample...");
        menu.addItem (2, "Clear pad", hasFile);

        juce::Component::SafePointer<PadButton> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int result)
        {
            if (safe == nullptr) return;
            if (result == 1 && safe->grid.onBrowse) safe->grid.onBrowse (safe->pad);
            if (result == 2) safe->processor.clearPad (safe->pad);
        });
        return;
    }

    if (hasSample)
    {
        pressed = true;
        processor.triggerPadFromUI (pad, true);
        repaint();
    }
}

void PadButton::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    if (pressed)
    {
        pressed = false;
        processor.triggerPadFromUI (pad, false);   // only looping pads react to the release
        repaint();
    }
    else if (! hasSample && ! loading && e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()))
    {
        if (grid.onBrowse)
            grid.onBrowse (pad);                    // empty (or missing) pad: choose a file
    }
}

bool PadButton::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (processor.getLoader().canRead (juce::File (f)))
            return true;
    return false;
}

void PadButton::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;

    // several files dropped at once fill the following pads too
    int target = pad;
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (target >= SimpleSamplerProcessor::numPads)
            break;
        if (processor.getLoader().canRead (file))
            processor.loadSampleIntoPad (target++, file, true);
    }

    if (grid.onSelect)
        grid.onSelect (pad);
    repaint();
}

//==============================================================================
PadGrid::PadGrid (SimpleSamplerProcessor& p) : processor (p)
{
    for (int i = 0; i < SimpleSamplerProcessor::numPads; ++i)
        addAndMakeVisible (buttons.add (new PadButton (*this, processor, i)));
}

void PadGrid::setSelectedPad (int padIndex)
{
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setSelected (i == padIndex);
}

void PadGrid::refreshPad (int padIndex)
{
    if (auto* b = buttons[padIndex])
        b->refreshContent();
}

void PadGrid::refreshAll()
{
    for (auto* b : buttons)
        b->refreshContent();
}

void PadGrid::updateGlow()
{
    for (auto* b : buttons)
        b->updateGlow();
}

void PadGrid::resized()
{
    const auto area = getLocalBounds();
    const float cw = (float) area.getWidth() / 4.0f, ch = (float) area.getHeight() / 4.0f;

    for (int i = 0; i < buttons.size(); ++i)
    {
        const int col = i % 4;
        const int row = 3 - i / 4;      // pad 1 starts at the bottom-left
        buttons[i]->setBounds (juce::Rectangle<float> ((float) area.getX() + cw * (float) col, (float) area.getY() + ch * (float) row, cw, ch)
                                   .toNearestInt());
    }
}
