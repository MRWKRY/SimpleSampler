#include "PluginEditor.h"

namespace
{
    constexpr int margin = 16;
    constexpr int headerHeight = 46;
    constexpr int gap = 12;
    constexpr int minWidth = 1040, minHeight = 690;
}

SimpleSamplerEditor::SimpleSamplerEditor (SimpleSamplerProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      waveform (p), grid (p), bpmCard (p), pitchCard (p), loopCard (p), envelopeCard (p), presetBar (p),
      tabs (p), tonePanel (p), genrePanel (p), modPanel (p)
{
    addAndMakeVisible (presetBar);
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);

    addAndMakeVisible (waveform);
    addAndMakeVisible (grid);
    addAndMakeVisible (bpmCard);
    addAndMakeVisible (pitchCard);
    addAndMakeVisible (loopCard);
    addAndMakeVisible (envelopeCard);

    addAndMakeVisible (tabs);
    addChildComponent (tonePanel);
    addChildComponent (genrePanel);
    addChildComponent (modPanel);
    tabs.onTabChanged = [this] (int tab) { showTab (tab); };
    tabs.onModSelected = [this] (int mod)
    {
        modPanel.setMod (mod);
        tabs.setSelectedMod (mod);
        showTab (PanelTabs::modTab);
    };
    tabs.setSelectedMod (0);

    masterKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    masterKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    masterKnob.setDoubleClickReturnValue (true, 0.0);
    masterKnob.setTooltip ("Output volume");
    masterKnob.onValueChange = [this] { repaint (headerArea()); };
    addAndMakeVisible (masterKnob);
    masterAttachment = std::make_unique<juce::SliderParameterAttachment> (*processor.getMasterParameter(), masterKnob);

    grid.onSelect = [this] (int pad) { selectPad (pad); };
    grid.onBrowse = [this] (int pad) { browseForPad (pad); };
    waveform.onBrowse = [this] (int pad) { browseForPad (pad); };

    lastStateVersion = processor.stateVersion.load();
    for (int i = 0; i < SimpleSamplerProcessor::numPads; ++i)
        lastContentVersion[i] = processor.getPad (i).contentVersion.load();

    selectPad (processor.getSelectedPad(), true);
    showTab (processor.editorTab.load());

    setResizable (true, true);
    setResizeLimits (minWidth, minHeight, 2200, 1500);
    setSize (juce::jlimit (minWidth, 2200, processor.editorWidth.load()),
             juce::jlimit (minHeight, 1500, processor.editorHeight.load()));

    startTimerHz (60);
}

SimpleSamplerEditor::~SimpleSamplerEditor()
{
    stopTimer();
    chooser.reset();
    setLookAndFeel (nullptr);
}

juce::Rectangle<int> SimpleSamplerEditor::headerArea() const
{
    return getLocalBounds().reduced (margin, 0).withTrimmedTop (margin - 4).withHeight (headerHeight);
}

//==============================================================================
void SimpleSamplerEditor::paint (juce::Graphics& g)
{
    if (background.isValid())
        g.drawImageAt (background, 0, 0);
    else
        PastelLookAndFeel::drawBackground (g, getLocalBounds());

    auto header = headerArea();

    // ---- own logo mark: a rounded tile with four pad dots
    auto logo = header.removeFromLeft (34).withSizeKeepingCentre (30, 30).toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8b5cf6), logo.getX(), logo.getY(),
                                             juce::Colour (0xff7cc4f5), logo.getRight(), logo.getBottom(), false));
    g.fillRoundedRectangle (logo, 9.0f);
    g.setColour (juce::Colours::white);
    for (int i = 0; i < 4; ++i)
    {
        const float cx = logo.getCentreX() + ((i % 2) ? 5.0f : -5.0f);
        const float cy = logo.getCentreY() + ((i / 2) ? 5.0f : -5.0f);
        g.setOpacity (i == 2 ? 1.0f : 0.6f);
        g.fillRoundedRectangle (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ cx, cy }), 2.2f);
    }
    g.setOpacity (1.0f);

    header.removeFromLeft (10);
    auto titleArea = header.removeFromLeft (190);
    g.setColour (PastelLookAndFeel::text());
    g.setFont (PastelLookAndFeel::font (19.0f, true));
    g.drawText ("SimpleSampler", titleArea.removeFromTop (27), juce::Justification::bottomLeft, false);
    g.setColour (PastelLookAndFeel::textSoft());
    g.setFont (PastelLookAndFeel::font (11.5f));
    g.drawText ("16 pads / stretch + pitch", titleArea, juce::Justification::topLeft, false);

    // ---- output volume read-out next to the knob
    const auto knob = masterKnob.getBounds();
    auto textArea = juce::Rectangle<int> (knob.getX() - 78, knob.getY(), 72, knob.getHeight());
    g.setColour (PastelLookAndFeel::textSoft());
    g.setFont (PastelLookAndFeel::font (10.5f));
    g.drawText ("OUTPUT", textArea.removeFromTop (textArea.getHeight() / 2), juce::Justification::bottomRight, false);
    g.setColour (PastelLookAndFeel::text());
    g.setFont (PastelLookAndFeel::font (12.5f, true));
    const double db = masterKnob.getValue();
    g.drawText (db <= -59.95 ? juce::String ("-inf dB") : juce::String (db, 1) + " dB", textArea, juce::Justification::topRight, false);
}

void SimpleSamplerEditor::resized()
{
    processor.editorWidth.store (getWidth());
    processor.editorHeight.store (getHeight());

    // pre-render the background once per size
    background = juce::Image (juce::Image::RGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), false);
    {
        juce::Graphics g (background);
        PastelLookAndFeel::drawBackground (g, getLocalBounds());
    }

    auto header = headerArea();
    masterKnob.setBounds (header.removeFromRight (44).withSizeKeepingCentre (44, 44));

    // preset bar: lines up with the right panel, between the title and the output read-out
    {
        auto bar = header;
        bar.removeFromRight (90);
        bar.removeFromLeft (250);
        const int width = juce::jlimit (200, 360, bar.getWidth());
        presetBar.setBounds (bar.removeFromRight (width).withSizeKeepingCentre (width, 36));
    }

    auto area = getLocalBounds().reduced (margin);
    area.removeFromTop (headerHeight + gap - 4);

    // ---- right panel: four cards
    auto right = area.removeFromRight (juce::jlimit (350, 420, juce::roundToInt ((float) getWidth() * 0.335f)));
    area.removeFromRight (gap);

    tabs.setBounds (right.removeFromTop (32));
    right.removeFromTop (8);
    tonePanel.setBounds (right);
    genrePanel.setBounds (right);
    modPanel.setBounds (right);

    const int cardGap = 10;
    bpmCard.setBounds (right.removeFromTop (96));
    right.removeFromTop (cardGap);
    pitchCard.setBounds (right.removeFromTop (96));
    right.removeFromTop (cardGap);
    loopCard.setBounds (right.removeFromTop (96));
    right.removeFromTop (cardGap);
    envelopeCard.setBounds (right);

    // ---- left: waveform above the pad grid
    auto pads = area.removeFromBottom (juce::jmax (190, juce::roundToInt ((float) area.getHeight() * 0.46f)));
    area.removeFromBottom (gap - 3);
    waveform.setBounds (area);
    grid.setBounds (pads.expanded (3, 3));
}

//==============================================================================
void SimpleSamplerEditor::selectPad (int padIndex, bool force)
{
    padIndex = juce::jlimit (0, SimpleSamplerProcessor::numPads - 1, padIndex);
    if (padIndex == shownPad && ! force)
        return;

    shownPad = padIndex;
    processor.setSelectedPad (padIndex);

    grid.setSelectedPad (padIndex);
    waveform.setPad (padIndex);
    bpmCard.setPad (padIndex);
    pitchCard.setPad (padIndex);
    loopCard.setPad (padIndex);
    envelopeCard.setPad (padIndex);
    tonePanel.setPad (padIndex);
    genrePanel.setPad (padIndex);
    refreshSelectedPadContent();
}

void SimpleSamplerEditor::refreshSelectedPadContent()
{
    waveform.refreshContent();
    const auto sample = processor.getPad (shownPad).getSample();
    loopCard.setSampleLength (sample != nullptr ? sample->lengthSeconds : 0.0);
}

void SimpleSamplerEditor::browseForPad (int padIndex)
{
    auto start = lastBrowseFolder;
    if (! start.isDirectory())
    {
        const auto current = processor.getPad (padIndex).getFile();
        start = current.existsAsFile() ? current.getParentDirectory()
                                       : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    }

    chooser = std::make_unique<juce::FileChooser> ("Choose a sample for pad " + juce::String (padIndex + 1),
                                                   start, processor.getLoader().getWildcard());

    juce::Component::SafePointer<SimpleSamplerEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, padIndex] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr)
                                  return;

                              const auto file = fc.getResult();
                              if (file.existsAsFile())
                              {
                                  safe->lastBrowseFolder = file.getParentDirectory();
                                  safe->processor.loadSampleIntoPad (padIndex, file, true);
                              }
                          });
}

void SimpleSamplerEditor::timerCallback()
{
    // the whole state was replaced (project load / preset load)
    const auto stateVersion = processor.stateVersion.load();
    if (stateVersion != lastStateVersion)
    {
        lastStateVersion = stateVersion;
        selectPad (processor.getSelectedPad(), true);
    showTab (processor.editorTab.load());
        grid.refreshAll();
        presetBar.refresh();
        showTab (processor.editorTab.load());
    }

    // samples that finished loading / were cleared
    for (int i = 0; i < SimpleSamplerProcessor::numPads; ++i)
    {
        const auto version = processor.getPad (i).contentVersion.load();
        if (version != lastContentVersion[i])
        {
            lastContentVersion[i] = version;
            grid.refreshPad (i);
            if (i == shownPad)
                refreshSelectedPadContent();
        }
    }

    grid.updateGlow();
    waveform.setPlayPosition (processor.getPad (shownPad).playPosition.load (std::memory_order_relaxed));
}

//==============================================================================
juce::AudioProcessorEditor* SimpleSamplerProcessor::createEditor()
{
    return new SimpleSamplerEditor (*this);
}

/** Right panel: Sample (the original four cards) / Tone-FX / Genre / Mod. */
void SimpleSamplerEditor::showTab (int tab)
{
    tab = juce::jlimit (0, (int) PanelTabs::numTabs - 1, tab);
    processor.editorTab.store (tab);
    tabs.setTab (tab);

    const bool sample = tab == PanelTabs::sampleTab;
    bpmCard.setVisible (sample);
    pitchCard.setVisible (sample);
    loopCard.setVisible (sample);
    envelopeCard.setVisible (sample);
    tonePanel.setVisible (tab == PanelTabs::toneTab);
    genrePanel.setVisible (tab == PanelTabs::genreTab);
    modPanel.setVisible (tab == PanelTabs::modTab);
}
