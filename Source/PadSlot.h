#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>

/** Immutable, fully loaded sample (always 2 channels, already resampled to the host rate). */
class SampleData : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

    static constexpr int numPeaks = 2048;

    juce::AudioBuffer<float> buffer;        // 2 channels
    double sampleRate       = 44100.0;      // rate of `buffer` (= host rate when loaded)
    double sourceSampleRate = 44100.0;      // rate of the file on disk
    int    sourceChannels   = 2;
    double lengthSeconds    = 0.0;
    juce::String name;

    // min/max overview for drawing (numPeaks buckets, mixed to mono)
    std::vector<float> peakMin, peakMax;

    int numSamples() const noexcept { return buffer.getNumSamples(); }
    void buildPeaks();
};

/** Parameter values of one pad, captured once at note-on (never change while a voice plays). */
struct PadSettings
{
    double speedRatio  = 1.0;   // result BPM / start BPM
    double semitones   = 0.0;
    bool   loop        = false;
    double loopStart   = 0.0;   // normalised 0..1
    double loopEnd     = 1.0;
    float  attackSec   = 0.001f;
    float  decaySec    = 0.1f;
    float  sustain     = 1.0f;
    float  releaseSec  = 0.05f;

    // version 2 (captured at note-on like everything else)
    float  diveSemitones    = 0.0f;   // pitch offset at the start of the note, eases back to 0
    float  diveSeconds      = 0.25f;
    float  formantSemitones = 0.0f;
    bool   forceStretch     = false;  // use the stretch engine even at original speed (keeps tempo exact while the pitch moves)
};

/** One of the 16 pads: its parameters, its sample and UI feedback values. */
class PadSlot
{
public:
    PadSlot() = default;

    void attachParameters (juce::AudioProcessorValueTreeState& apvts, int padIndex);

    // ---- audio thread ------------------------------------------------------
    int  getNote() const noexcept               { return juce::roundToInt (note->load()); }
    bool isLooping() const noexcept             { return loop->load() > 0.5f; }
    PadSettings readSettings() const noexcept;
    /** Wait-free: returns nullptr if the sample is being swapped right now. */
    SampleData::Ptr getSampleForAudio() const noexcept;

    // ---- any non-audio thread ---------------------------------------------
    SampleData::Ptr getSample() const;
    void setSample (SampleData::Ptr newSample);

    void setFile (const juce::File& f, const juce::String& fileNameIfMissing = {});
    juce::File   getFile() const;
    juce::String getFileName() const;
    void setMissing (bool m) noexcept           { missing.store (m); }
    bool isMissing() const noexcept             { return missing.load(); }
    bool hasFile() const;

    // ---- UI feedback (written by the audio thread) --------------------------
    std::atomic<int>      activeVoices   { 0 };
    std::atomic<uint32_t> triggerCount   { 0 };
    std::atomic<float>    playPosition   { -1.0f };  // normalised, <0 = not playing
    std::atomic<uint32_t> contentVersion { 0 };      // bumped when the sample / file changes
    std::atomic<bool>     loading        { false };
    std::atomic<int>      loadGeneration { 0 };

    // raw parameter values (owned by the APVTS)
    std::atomic<float>* note = nullptr;
    std::atomic<float>* srcBpm = nullptr;
    std::atomic<float>* dstBpm = nullptr;
    std::atomic<float>* srcPitch = nullptr;
    std::atomic<float>* semitones = nullptr;
    std::atomic<float>* loop = nullptr;
    std::atomic<float>* loopStart = nullptr;
    std::atomic<float>* loopEnd = nullptr;
    std::atomic<float>* attack = nullptr;
    std::atomic<float>* decay = nullptr;
    std::atomic<float>* sustain = nullptr;
    std::atomic<float>* release = nullptr;

private:
    mutable juce::SpinLock sampleLock;
    SampleData::Ptr sample;

    mutable juce::CriticalSection fileLock;
    juce::File file;
    juce::String fileName;
    std::atomic<bool> missing { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadSlot)
};
