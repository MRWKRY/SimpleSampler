#pragma once

#include "PluginProcessor.h"

/** Saves / loads the complete plugin state (all pads, sample paths, parameters) as preset files.
    Presets live in <Documents>/SimpleSampler/Presets as small XML files (*.sspreset). */
class PresetManager
{
public:
    explicit PresetManager (SimpleSamplerProcessor&, juce::File folder = getDefaultFolder());

    static juce::File getDefaultFolder();
    static constexpr const char* extension = ".sspreset";

    juce::File getFolder() const                            { return folder; }
    juce::File fileForName (const juce::String& name) const;
    static juce::String cleanName (const juce::String& name);

    /** Preset names, sorted alphabetically (case-insensitive). */
    juce::StringArray getPresetNames() const;
    bool presetExists (const juce::String& name) const;

    bool savePreset (const juce::String& name);
    bool loadPreset (const juce::String& name);
    bool loadPresetFile (const juce::File& file);
    bool deletePreset (const juce::String& name);

    /** Step through the preset list (wraps around). */
    bool loadNext()                                         { return step (1); }
    bool loadPrevious()                                     { return step (-1); }

    juce::String getCurrentName() const                     { return processor.getPresetName(); }

private:
    bool step (int direction);

    SimpleSamplerProcessor& processor;
    juce::File folder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};
