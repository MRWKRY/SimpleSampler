#pragma once

#include "PluginProcessor.h"
#include "PastelLookAndFeel.h"
#include "WaveformView.h"
#include "PadGrid.h"
#include "ControlCards.h"
#include "PresetBar.h"

class SimpleSamplerEditor : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit SimpleSamplerEditor (SimpleSamplerProcessor&);
    ~SimpleSamplerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void selectPad (int padIndex, bool force = false);
    void browseForPad (int padIndex);
    void refreshSelectedPadContent();

    juce::Rectangle<int> headerArea() const;

    SimpleSamplerProcessor& processor;
    PastelLookAndFeel lookAndFeel;

    WaveformView waveform;
    PadGrid grid;
    BpmCard bpmCard;
    PitchCard pitchCard;
    LoopCard loopCard;
    EnvelopeCard envelopeCard;
    PresetBar presetBar;

    juce::Slider masterKnob;
    std::unique_ptr<juce::SliderParameterAttachment> masterAttachment;

    juce::TooltipWindow tooltipWindow { this, 700 };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastBrowseFolder;

    juce::Image background;
    int shownPad = -1;
    uint32_t lastStateVersion = 0;
    uint32_t lastContentVersion[SimpleSamplerProcessor::numPads] = {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleSamplerEditor)
};
