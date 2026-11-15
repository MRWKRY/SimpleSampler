#include "PluginProcessor.h"

// Stage 1: generic parameter list. Replaced by the real editor in stage 2.
juce::AudioProcessorEditor* SimpleSamplerProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}
