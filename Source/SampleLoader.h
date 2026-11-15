#pragma once

#include "PadSlot.h"
#include <juce_audio_formats/juce_audio_formats.h>

/** Reads audio files (WAV / AIFF / FLAC / OGG) and converts them to the host sample rate.
    Everything here runs on a background thread, never on the audio thread. */
class SampleLoader
{
public:
    SampleLoader();
    ~SampleLoader();

    static constexpr double maxLengthSeconds = 600.0;

    /** Blocking. targetRate <= 0 keeps the file's own sample rate. Returns nullptr on failure. */
    SampleData::Ptr loadFile (const juce::File& file, double targetRate, juce::String& error);

    /** Runs a job on the single background loading thread. */
    void runAsync (std::function<void()> job);
    void cancelAll();

    juce::String getWildcard() const                { return formatManager.getWildcardForAllFormats(); }
    bool canRead (const juce::File& file) const;

private:
    juce::AudioFormatManager formatManager;
    juce::ThreadPool pool { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleLoader)
};
