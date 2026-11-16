#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "ParamMath.h"
#include "PadSlot.h"
#include "Voice.h"
#include "SampleLoader.h"
#include "FX/PadChannel.h"
#include "FX/GlobalFx.h"
#include "Modulation/ModMatrix.h"
#include "Modulation/Modulator.h"
#include "Modulation/Randomizer.h"
#include <array>

class SimpleSamplerProcessor : public juce::AudioProcessor
{
public:
    SimpleSamplerProcessor();
    ~SimpleSamplerProcessor() override;

    static constexpr int numPads = ParamMath::numPads;
    static constexpr int maxVoices = ParamMath::maxVoices;
    static constexpr int numPhysicalVoices = maxVoices + 8;   // spare voices cover the short steal fade-out
    static constexpr int numMods = ParamMath::numMods;
    static constexpr int numPadFx = ParamMath::id::numPadFx;
    static constexpr int maxSegment = fx::GlobalFx::maxSegment;

    /** Format version written into presets and the plugin state.
        1 = first release (sampler only), 2 = mixer / FX / genre knobs / modulation / randomiser. */
    static constexpr int currentStateVersion = 2;

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
    double getTailLengthSeconds() const override            { return 10.0; }   // reverb / delay tails

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
    /** Empties every pad and puts all parameters back to their defaults. */
    void resetToInit();

    /** Pad pressed / released in the editor (thread-safe, handled on the audio thread). */
    void triggerPadFromUI (int padIndex, bool isDown) noexcept;

    int  getSelectedPad() const noexcept                    { return selectedPad.load(); }
    void setSelectedPad (int index) noexcept                { selectedPad.store (juce::jlimit (0, numPads - 1, index)); }

    juce::RangedAudioParameter* getPadParameter (int padIndex, const char* name) const;
    juce::RangedAudioParameter* getMasterParameter() const;

    SampleLoader& getLoader() noexcept                      { return loader; }
    int getActiveVoiceCount() const noexcept                { return totalActiveVoices.load(); }
    /** Current gain of the output limiter (1 = not limiting). Not thread-safe: tests / diagnostics only. */
    float getLimiterGain() const noexcept                   { return globalFx.getLimiterGain(); }

    // ---- modulation / randomiser ---------------------------------------------
    ModMatrix& getModMatrix() noexcept                      { return modMatrix; }
    Randomizer& getRandomizer() noexcept                    { return *randomizer; }
    const Modulator& getModulator (int index) const noexcept { return modulators[(size_t) juce::jlimit (0, numMods - 1, index)]; }
    /** Index of a pad's tone / genre knob in the ModMatrix (see ParamMath::id::PadFx). */
    int getPadTarget (int padIndex, int padFx) const noexcept { return padTargets[juce::jlimit (0, numPads - 1, padIndex)][juce::jlimit (0, numPadFx - 1, padFx)]; }

    /** Tempo currently in use, and whether it comes from the host (otherwise: the manual tempo). */
    double getCurrentBpm() const noexcept                   { return uiBpm.load(); }
    bool isUsingHostTempo() const noexcept                  { return uiHostTempo.load(); }

    // ---- state / presets ---------------------------------------------------
    static const juce::Identifier stateType;
    juce::ValueTree createStateTree() const;
    /** `searchDir` is an extra folder searched for samples whose absolute path no longer exists.
        Older formats are accepted: everything they do not contain is set to its default. */
    bool restoreStateTree (const juce::ValueTree& tree, const juce::File& searchDir = {});

    juce::String getPresetName() const;
    void setPresetName (const juce::String& name);

    /** Bumped whenever the whole state was replaced (the editor polls this). */
    std::atomic<uint32_t> stateVersion { 0 };

    /** Last editor size / tab (restored when the editor is reopened / the project is loaded). */
    std::atomic<int> editorWidth { 1140 }, editorHeight { 720 }, editorTab { 0 };

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
    void processSegment (juce::AudioBuffer<float>& buffer, int start, int num) noexcept;
    void readBlockSettings() noexcept;
    void killAllVoices() noexcept;

    std::array<PadSlot, (size_t) numPads> pads;
    std::array<Voice, (size_t) numPhysicalVoices> voices;
    juce::uint64 voiceCounter = 0;

    // ---- mixer, effects, modulation
    ModMatrix modMatrix;
    std::unique_ptr<Randomizer> randomizer;
    std::array<Modulator, (size_t) numMods> modulators;
    TempoClock clock;

    std::array<fx::PadChannel, (size_t) numPads> channels;
    fx::GlobalFx globalFx;
    fx::GenreSettings genre;

    int padTargets[numPads][numPadFx] = {};
    enum GlobalTarget { gRvSize = 0, gRvDecay, gRvPreDelay, gRvLowCut, gRvHighCut, gRvMix,
                        gDlTime, gDlFeedback, gDlLowCut, gDlHighCut, gDlMix, gPumpRelease, numGlobalTargets };
    int globalTargets[numGlobalTargets] = {};

    std::atomic<float>* masterGainDb = nullptr;
    std::atomic<float>* manualTempo = nullptr;
    std::atomic<float>* delaySync = nullptr;
    std::atomic<float>* delayDivision = nullptr;
    std::atomic<float>* delayPingPong = nullptr;
    std::atomic<float>* pumpRate = nullptr;
    std::atomic<float>* wobbleRate = nullptr;
    std::atomic<float>* gateRate = nullptr;
    std::atomic<float>* gateSteps[TempoSync::numSteps] = {};
    std::atomic<float>* diveRange = nullptr;
    std::atomic<float>* diveTime = nullptr;
    std::atomic<float>* formantRange = nullptr;

    float padBuffers[numPads][2][maxSegment] = {};
    float formantNow[numPads] = {};

    std::atomic<double> currentRate { 0.0 };
    std::atomic<uint32_t> uiNoteOn { 0 }, uiNoteOff { 0 };
    std::atomic<int> selectedPad { 0 };
    std::atomic<int> totalActiveVoices { 0 };
    std::atomic<double> uiBpm { 120.0 };
    std::atomic<bool> uiHostTempo { false };

    juce::CriticalSection retainLock;
    juce::ReferenceCountedArray<SampleData> retained;   // keeps samples alive so the audio thread never frees one

    mutable juce::CriticalSection nameLock;
    juce::String presetName { "Init" };

    SampleLoader loader;                                 // last member: its thread stops first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleSamplerProcessor)
};
