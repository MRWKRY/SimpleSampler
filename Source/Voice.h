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

    /** Allocates everything. Not real-time safe; call from prepareToPlay. */
    void prepare (double sampleRate);

    /** Real-time safe. */
    void start (int padIndex, int midiNote, float velocity, SampleData::Ptr sample,
                const PadSettings& settings, juce::uint64 startOrder) noexcept;

    /** Adds this voice's audio into left/right. */
    void render (float* left, float* right, int numSamples) noexcept;

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

    void renderChunk (float* outL, float* outR, int n) noexcept;

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

    juce::AudioBuffer<float> inScratch, outScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Voice)
};
