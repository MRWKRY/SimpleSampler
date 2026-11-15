#pragma once

#include "PadSlot.h"
#include <memory>

/** Reads a sample forwards one frame at a time, handling the loop and its crossfade. */
class LoopReader
{
public:
    void start (const SampleData* data, bool loopEnabled, double loopStartNorm, double loopEndNorm,
                int crossfadeSamples) noexcept;

    /** Fills `numFrames` frames into left/right. Outputs silence after the end of a one-shot. */
    void read (float* left, float* right, int numFrames) noexcept;

    bool finished() const noexcept          { return ! looping && position >= length; }
    int  getPosition() const noexcept       { return position; }
    int  getLength() const noexcept         { return length; }
    bool isLooping() const noexcept         { return looping; }
    int  getLoopStart() const noexcept      { return loopStart; }
    int  getLoopEnd() const noexcept        { return loopEnd; }
    bool hasWrapped() const noexcept        { return wrapped; }

    enum class FadeMode { none, preRoll, postRoll, dip };
    FadeMode getFadeMode() const noexcept   { return fadeMode; }
    int getFadeLength() const noexcept      { return fadeLen; }

private:
    const float* srcL = nullptr;
    const float* srcR = nullptr;
    int length = 0, position = 0;
    bool looping = false, wrapped = false;
    int loopStart = 0, loopEnd = 0, fadeLen = 0;
    FadeMode fadeMode = FadeMode::none;
};

/** One playing instance of a pad. Owns its own time-stretch / pitch-shift engine. */
class Voice
{
public:
    Voice();
    ~Voice();

    static constexpr int chunkSize = 256;        // internal processing block
    static constexpr int maxSpeedRatio = 24;     // upper bound used to size scratch buffers
    static constexpr int resampleChunk = 64;     // block size while the pitch is moving
    static constexpr int maxPitchRatio = 8;      // +-36 semitones of pitch movement

    /** Allocates everything. Not real-time safe; call from prepareToPlay. */
    void prepare (double sampleRate);

    /** Real-time safe. */
    void start (int padIndex, int midiNote, float velocity, SampleData::Ptr sample,
                const PadSettings& settings, juce::uint64 startOrder) noexcept;

    /** Adds this voice's audio into left/right. */
    /** Adds this voice into left/right. pitchSemitones = extra pitch movement of the pad (Drift),
        formantSemitones = formant shift (only voices using the stretch engine can apply it). */
    void render (float* left, float* right, int numSamples, float pitchSemitones = 0.0f, float formantSemitones = 0.0f) noexcept;

    void noteOff() noexcept;            // enters the release stage
    void kill() noexcept;               // very short fade-out (voice stealing)
    void stopNow() noexcept;            // immediately frees the voice

    bool isActive() const noexcept              { return active; }
    bool isBeingKilled() const noexcept         { return killing; }
    bool isLoopVoice() const noexcept           { return settings.loop; }
    int  getPad() const noexcept                { return pad; }
    int  getNote() const noexcept               { return note; }
    juce::uint64 getStartOrder() const noexcept { return order; }

    /** Position (0..1 of the sample) that is currently audible. */
    float getNormalisedPosition() const noexcept;

    /** True when the voice plays the sample untouched (no stretch engine involved). */
    bool isBypassingStretch() const noexcept    { return bypass; }

private:
    struct Engine;                       // wraps signalsmith::stretch
    std::unique_ptr<Engine> engine;

    void produce (float* outL, float* outR, int count, double timeScale) noexcept;
    void renderResampled (float* outL, float* outR, int n, float ratioStart, float ratioEnd) noexcept;
    float currentDive() const noexcept;

    bool active = false, killing = false, bypass = true, released = false;
    int pad = -1, note = -1;
    float gain = 1.0f;
    juce::uint64 order = 0;

    SampleData::Ptr sample;
    PadSettings settings;
    LoopReader reader;
    juce::ADSR adsr;

    double hostRate = 44100.0;
    double inputAccumulator = 0.0;       // fractional input frames carried between chunks
    int latencyFrames = 0;               // input frames the reader is ahead of what is heard
    int tailRemaining = 0;               // output frames left after the source ran out
    float killGain = 1.0f, killStep = 0.0f;

    juce::AudioBuffer<float> inScratch, outScratch, srcScratch;
    int inCapacity = 0;

    // variable-speed stage used for pitch movement (Dive / Drift)
    float history[2][4] = {};
    double resampleFraction = 0.0;
    bool resampling = false;
    float previousRatio = 1.0f, lastFormant = 0.0f;
    juce::int64 diveElapsed = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Voice)
};
