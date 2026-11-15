#pragma once

#include "PluginProcessor.h"
#include "NumberField.h"

/** Waveform of the selected pad with a time ruler, draggable loop handles and a play cursor. */
class WaveformView : public juce::Component,
                     public juce::FileDragAndDropTarget
{
public:
    explicit WaveformView (SimpleSamplerProcessor&);

    void setPad (int padIndex);
    /** Call when the pad's sample or file changed. */
    void refreshContent();
    /** Normalised play position; a negative value hides the cursor. */
    void setPlayPosition (float normalised);

    /** Asks the editor to show a file chooser for the given pad. */
    std::function<void (int)> onBrowse;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override    { dragOver = true;  repaint(); }
    void fileDragExit (const juce::StringArray&) override               { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    enum Handle { none, startHandle, endHandle };

    juce::Rectangle<int> innerArea() const;
    juce::Rectangle<int> headerArea() const;
    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> waveArea() const;

    float xForNorm (double n) const;
    double normForX (float x) const;
    Handle handleAt (juce::Point<int> p) const;
    void rebuildCache (float scale);
    void paintRuler (juce::Graphics&);
    void paintLoop (juce::Graphics&);
    void repaintCursor (float normalised);

    SimpleSamplerProcessor& processor;
    int pad = 0;

    SampleData::Ptr sample;
    juce::String fileName;
    bool missing = false, loading = false;

    juce::Image cache;
    bool cacheDirty = true;
    float cacheScale = 0.0f;

    NumberField noteField;
    juce::TextButton loadButton { "Load" }, clearButton { "Clear" };
    std::unique_ptr<ParamField> noteParam;

    juce::RangedAudioParameter* loopParam = nullptr;
    juce::RangedAudioParameter* startParam = nullptr;
    juce::RangedAudioParameter* endParam = nullptr;
    std::unique_ptr<juce::ParameterAttachment> loopAttachment, startAttachment, endAttachment;

    Handle dragging = none;
    float playPosition = -1.0f;
    bool dragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};
