#pragma once

#include "PluginProcessor.h"

/** Small ADSR curve that can be dragged directly (three handles). */
class EnvelopeEditor : public juce::Component
{
public:
    explicit EnvelopeEditor (SimpleSamplerProcessor&);

    void setPad (int padIndex);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum Handle { none = -1, attackHandle, decayHandle, releaseHandle };

    struct Geometry
    {
        juce::Rectangle<float> area;
        float segment = 0, hold = 0;
        juce::Point<float> start, peak, sustainStart, sustainEnd, end;
    };

    Geometry getGeometry() const;
    Handle handleAt (juce::Point<float> p) const;
    float norm (int which) const;       // 0..1 position of a parameter
    void setNorm (int which, float n);

    SimpleSamplerProcessor& processor;
    int pad = 0;

    // 0 = attack, 1 = decay, 2 = sustain, 3 = release
    juce::RangedAudioParameter* params[4] = {};
    std::unique_ptr<juce::ParameterAttachment> attachments[4];

    Handle hover = none, active = none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvelopeEditor)
};
