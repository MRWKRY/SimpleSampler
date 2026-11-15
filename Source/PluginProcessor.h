#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "ParamMath.h"
#include "PadSlot.h"
#include "Voice.h"
#include "SampleLoader.h"
#include <array>

class SimpleSamplerProcessor : public juce::AudioProcessor
{
public:
    SimpleSamplerProcessor();
    ~SimpleSamplerProcessor() override;

    static constexpr int numPads = ParamMath::numPads;
    static constexpr int maxVoices = ParamMath::maxVoices;
    static constexpr int numPhysicalVoices = maxVoices + 8;   // spare voices cover the short steal fade-out

    // ---- AudioProcessor ----------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return "SimpleSampler"; }
    bool acceptsMidi() const override                       { return true; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 0.0; }

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- pads --------------------------------------------------------------
    PadSlot& getPad (int index) noexcept                    { return pads[(size_t) juce::jlimit (0, numPads - 1, index)]; }
    const PadSlot& getPad (int index) const noexcept        { return pads[(size_t) juce::jlimit (0, numPads - 1, index)]; }

    /** Loads a file into a pad on the background thread. `resetLoop` puts the loop back to the whole sample. */
    void loadSampleIntoPad (int padIndex, const juce::File& file, bool resetLoop);
    /** Same, but waits until the sample is ready (tests / offline use only). */
    bool loadSampleIntoPadBlocking (int padIndex, const juce::File& file);
    void clearPad (int padIndex);

    /** Pad pressed / released in the editor (thread-safe, handled on the audio thread). */
    void triggerPadFromUI (int padIndex, bool isDown) noexcept;

    int  getSelectedPad() const noexcept                    { return selectedPad.load(); }
    void setSelectedPad (int index) noexcept                { selectedPad.store (juce::jlimit (0, numPads - 1, index)); }

    juce::RangedAudioParameter* getPadParameter (int padIndex, const char* name) const;
    juce::RangedAudioParameter* getMasterParameter() const;

    SampleLoader& getLoader() noexcept                      { return loader; }
    int getActiveVoiceCount() const noexcept                { return totalActiveVoices.load(); }

    // ---- state / presets ---------------------------------------------------
    static const juce::Identifier stateType;
    juce::ValueTree createStateTree() const;
    /** `searchDir` is an extra folder searched for samples whose absolute path no longer exists. */
    bool restoreStateTree (const juce::ValueTree& tree, const juce::File& searchDir = {});

    juce::String getPresetName() const;
    void setPresetName (const juce::String& name);

    /** Bumped whenever the whole state was replaced (the editor polls this). */
    std::atomic<uint32_t> stateVersion { 0 };

    /** Last editor size (restored when the editor is reopened / the project is loaded). */
    std::atomic<int> editorWidth { 1000 }, editorHeight { 620 };

    juce::AudioProcessorValueTreeState apvts;

    static juce::File getUserFolder();          // <Documents>/SimpleSampler

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void queueLoad (int padIndex);
    void retainSample (const SampleData::Ptr& s);
    void pruneRetained();

    // audio thread
    void handleMidi (const juce::MidiMessage& m) noexcept;
    void startPad (int padIndex, int midiNote, float velocity) noexcept;
    void releaseNote (int midiNote) noexcept;
    void renderVoices (juce::AudioBuffer<float>& buffer, int start, int num) noexcept;
    void killAllVoices() noexcept;

    std::array<PadSlot, (size_t) numPads> pads;
    std::array<Voice, (size_t) numPhysicalVoices> voices;
    juce::uint64 voiceCounter = 0;

    std::atomic<float>* masterGainDb = nullptr;
    juce::SmoothedValue<float> masterGain;

    std::atomic<double> currentRate { 0.0 };
    std::atomic<uint32_t> uiNoteOn { 0 }, uiNoteOff { 0 };
    std::atomic<int> selectedPad { 0 };
    std::atomic<int> totalActiveVoices { 0 };

    juce::CriticalSection retainLock;
    juce::ReferenceCountedArray<SampleData> retained;   // keeps samples alive so the audio thread never frees one

    mutable juce::CriticalSection nameLock;
    juce::String presetName { "Init" };

    SampleLoader loader;                                 // last member: its thread stops first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleSamplerProcessor)
};
