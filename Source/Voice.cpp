#include "Voice.h"
#include "ParamMath.h"

JUCE_BEGIN_IGNORE_WARNINGS_MSVC (4100 4127 4244 4245 4267 4305 4456 4457 4458 4459 4701 4702 4706)
#include "signalsmith-stretch/signalsmith-stretch.h"
JUCE_END_IGNORE_WARNINGS_MSVC

//==============================================================================
void LoopReader::start (const SampleData* data, bool loopEnabled, double loopStartNorm,
                        double loopEndNorm, int crossfadeSamples) noexcept
{
    srcL = data->buffer.getReadPointer (0);
    srcR = data->buffer.getReadPointer (1);
    length = data->numSamples();
    position = 0;
    wrapped = false;
    fadeMode = FadeMode::none;
    fadeLen = 0;

    constexpr int minLoopLength = 64;

    int le = juce::jlimit (1, juce::jmax (1, length), juce::roundToInt (loopEndNorm * length));
    int ls = juce::jlimit (0, le - 1, juce::roundToInt (loopStartNorm * length));
    if (le - ls < minLoopLength) le = juce::jmin (length, ls + minLoopLength);
    if (le - ls < minLoopLength) ls = juce::jmax (0, le - minLoopLength);

    loopStart = ls;
    loopEnd = le;
    looping = loopEnabled && length > 0 && le > ls;

    if (! looping)
        return;

    const int loopLen = le - ls;
    const int x = juce::jmin (crossfadeSamples, loopLen / 2);
    if (x < 2)
        return;

    if (ls >= x)                 { fadeMode = FadeMode::preRoll;  fadeLen = x; }      // blend in audio from before the loop start
    else if (le + x <= length)   { fadeMode = FadeMode::postRoll; fadeLen = x; }      // blend out audio from after the loop end
    else                         { fadeMode = FadeMode::dip;      fadeLen = juce::jmax (1, x / 2); } // whole-file loop: tiny fade out / in
}

void LoopReader::read (float* left, float* right, int numFrames) noexcept
{
    if (! looping)
    {
        const int n = juce::jlimit (0, numFrames, length - position);
        for (int i = 0; i < n; ++i)
        {
            left[i]  = srcL[position + i];
            right[i] = srcR[position + i];
        }
        for (int i = n; i < numFrames; ++i)
            left[i] = right[i] = 0.0f;

        position += n;
        return;
    }

    const int loopLen = loopEnd - loopStart;

    for (int i = 0; i < numFrames; ++i)
    {
        const int p = position;
        float l = srcL[p], r = srcR[p];

        switch (fadeMode)
        {
            case FadeMode::preRoll:
                if (p >= loopEnd - fadeLen)
                {
                    const float t = (float) (p - (loopEnd - fadeLen) + 1) / (float) (fadeLen + 1);
                    const int q = p - loopLen;
                    l += (srcL[q] - l) * t;
                    r += (srcR[q] - r) * t;
                }
                break;

            case FadeMode::postRoll:
                if (wrapped && p < loopStart + fadeLen)
                {
                    const float t = (float) (p - loopStart + 1) / (float) (fadeLen + 1);
                    const int q = p + loopLen;
                    l = srcL[q] + (l - srcL[q]) * t;
                    r = srcR[q] + (r - srcR[q]) * t;
                }
                break;

            case FadeMode::dip:
                if (p >= loopEnd - fadeLen)
                {
                    const float g = (float) (loopEnd - 1 - p) / (float) fadeLen;
                    l *= g; r *= g;
                }
                else if (wrapped && p < loopStart + fadeLen)
                {
                    const float g = (float) (p - loopStart) / (float) fadeLen;
                    l *= g; r *= g;
                }
                break;

            case FadeMode::none:
            default:
                break;
        }

        left[i] = l;
        right[i] = r;

        if (++position >= loopEnd)
        {
            position = loopStart;
            wrapped = true;
        }
    }
}

//==============================================================================
struct Voice::Engine
{
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    int inputLatency = 0, outputLatency = 0;
};

Voice::Voice() : engine (std::make_unique<Engine>()) {}
Voice::~Voice() = default;

void Voice::prepare (double sampleRate)
{
    stopNow();
    hostRate = sampleRate;

    engine->stretch.presetDefault (2, (float) sampleRate);
    engine->inputLatency  = (int) engine->stretch.inputLatency();
    engine->outputLatency = (int) engine->stretch.outputLatency();

    const int inSize = juce::jmax (chunkSize * maxSpeedRatio, engine->inputLatency) + 16;
    inScratch.setSize (2, inSize, false, true, false);
    outScratch.setSize (2, chunkSize, false, true, false);

    adsr.setSampleRate (sampleRate);

    // run the engine once so that any lazily created state exists before real-time use
    inScratch.clear();
    outScratch.clear();
    engine->stretch.reset();
    float* in[2]  = { inScratch.getWritePointer (0),  inScratch.getWritePointer (1) };
    float* out[2] = { outScratch.getWritePointer (0), outScratch.getWritePointer (1) };
    engine->stretch.seek (in, engine->inputLatency, 1.0);
    engine->stretch.process (in, chunkSize, out, chunkSize);
    engine->stretch.reset();
}

void Voice::start (int padIndex, int midiNote, float velocity, SampleData::Ptr newSample,
                   const PadSettings& newSettings, juce::uint64 startOrder) noexcept
{
    if (newSample == nullptr || newSample->numSamples() <= 0)
        return;

    sample = std::move (newSample);
    settings = newSettings;
    settings.speedRatio = juce::jlimit (1.0 / maxSpeedRatio, (double) maxSpeedRatio, settings.speedRatio);

    pad = padIndex;
    note = midiNote;
    gain = velocity;
    order = startOrder;
    killing = false;
    released = false;
    killGain = 1.0f;
    inputAccumulator = 0.0;
    tailRemaining = -1;
    latencyFrames = 0;

    reader.start (sample.get(), settings.loop, settings.loopStart, settings.loopEnd,
                  juce::roundToInt (0.008 * hostRate));

    adsr.setSampleRate (hostRate);
    adsr.setParameters ({ settings.attackSec, settings.decaySec, settings.sustain, settings.releaseSec });
    adsr.reset();
    adsr.noteOn();

    // identical BPM and pitch -> play the sample untouched
    bypass = std::abs (settings.speedRatio - 1.0) < 1.0e-6 && std::abs (settings.semitones) < 1.0e-6;

    if (! bypass)
    {
        auto& st = engine->stretch;
        st.reset();
        st.setTransposeSemitones ((float) settings.semitones);

        float* in[2]  = { inScratch.getWritePointer (0),  inScratch.getWritePointer (1) };
        float* out[2] = { outScratch.getWritePointer (0), outScratch.getWritePointer (1) };

        // Pre-roll so that the first sample we output is the first sample of the file
        // (hides the engine's latency: the pad responds immediately).
        reader.read (in[0], in[1], engine->inputLatency);
        st.seek (in, engine->inputLatency, settings.speedRatio);

        int remaining = engine->outputLatency;
        while (remaining > 0)
        {
            const int n = juce::jmin (chunkSize, remaining);
            inputAccumulator += n * settings.speedRatio;
            const int nIn = (int) inputAccumulator;
            inputAccumulator -= nIn;
            reader.read (in[0], in[1], nIn);
            st.process (in, nIn, out, n);
            remaining -= n;
        }

        latencyFrames = engine->inputLatency + juce::roundToInt (engine->outputLatency * settings.speedRatio);
    }

    active = true;
}

void Voice::renderChunk (float* outL, float* outR, int n) noexcept
{
    if (bypass)
    {
        reader.read (outL, outR, n);
        if (reader.finished())
            tailRemaining = 0;
        return;
    }

    float* in[2]  = { inScratch.getWritePointer (0), inScratch.getWritePointer (1) };
    float* out[2] = { outL, outR };

    inputAccumulator += n * settings.speedRatio;
    const int nIn = (int) inputAccumulator;
    inputAccumulator -= nIn;

    const bool wasFinished = reader.finished();
    reader.read (in[0], in[1], nIn);
    engine->stretch.process (in, nIn, out, n);

    if (reader.finished())
    {
        if (! wasFinished || tailRemaining < 0)
            tailRemaining = engine->outputLatency + (int) (engine->inputLatency / settings.speedRatio) + n;

        tailRemaining = juce::jmax (0, tailRemaining - n);
    }
}

void Voice::render (float* left, float* right, int numSamples) noexcept
{
    int done = 0;

    while (active && done < numSamples)
    {
        const int n = juce::jmin (chunkSize, numSamples - done);
        float* tl = outScratch.getWritePointer (0);
        float* tr = outScratch.getWritePointer (1);

        renderChunk (tl, tr, n);

        for (int i = 0; i < n; ++i)
        {
            const float g = adsr.getNextSample() * gain * killGain;
            left[done + i]  += tl[i] * g;
            right[done + i] += tr[i] * g;

            if (killing)
            {
                killGain -= killStep;
                if (killGain <= 0.0f)
                {
                    killGain = 0.0f;
                    stopNow();
                    return;
                }
            }
        }

        done += n;

        if (tailRemaining == 0 || ! adsr.isActive())
            stopNow();
    }
}

void Voice::noteOff() noexcept
{
    if (active && ! released)
    {
        released = true;
        adsr.noteOff();
    }
}

void Voice::kill() noexcept
{
    if (active && ! killing)
    {
        killing = true;
        killStep = 1.0f / (float) juce::jmax (1.0, 0.005 * hostRate);
    }
}

void Voice::stopNow() noexcept
{
    active = false;
    killing = false;
    sample = nullptr;   // never the last reference (the processor keeps one), so no deallocation here
}

float Voice::getNormalisedPosition() const noexcept
{
    if (! active || reader.getLength() <= 0)
        return -1.0f;

    int p = reader.getPosition() - latencyFrames;
    if (reader.isLooping() && reader.hasWrapped() && p < reader.getLoopStart())
        p += reader.getLoopEnd() - reader.getLoopStart();

    return juce::jlimit (0.0f, 1.0f, (float) p / (float) reader.getLength());
}
