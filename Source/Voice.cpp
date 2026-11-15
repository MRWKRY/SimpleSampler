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

    const int maxSource = juce::jmax (chunkSize, resampleChunk * maxPitchRatio + 2);
    inCapacity = juce::jmax (maxSource * maxSpeedRatio, engine->inputLatency);
    inScratch.setSize (2, inCapacity + 16, false, true, false);
    outScratch.setSize (2, chunkSize, false, true, false);
    srcScratch.setSize (2, maxSource + 16, false, true, false);

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

    // pitch movement (Dive / Drift) state
    for (auto& h : history) for (auto& s : h) s = 0.0f;
    resampleFraction = 0.0;
    resampling = false;
    diveElapsed = 0;
    previousRatio = std::pow (2.0f, juce::jlimit (-36.0f, 36.0f, settings.diveSemitones) / 12.0f);
    lastFormant = settings.formantSemitones;

    // identical BPM and pitch -> play the sample untouched
    // (Dive / Drift / Formant need the stretch engine so that the tempo stays exact)
    bypass = std::abs (settings.speedRatio - 1.0) < 1.0e-6 && std::abs (settings.semitones) < 1.0e-6
             && ! settings.forceStretch;

    if (! bypass)
    {
        auto& st = engine->stretch;
        st.reset();
        st.setTransposeSemitones ((float) settings.semitones);
        st.setFormantSemitones (settings.formantSemitones);

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

/** Produces `count` frames of the voice's source stream.
    `timeScale` < 1 makes the stretch engine deliver "longer" audio: the resampler after it then
    raises the pitch again without changing the tempo. */
void Voice::produce (float* outL, float* outR, int count, double timeScale) noexcept
{
    if (count <= 0)
        return;

    if (bypass)
    {
        reader.read (outL, outR, count);
        if (reader.finished())
            tailRemaining = 0;
        return;
    }

    float* in[2]  = { inScratch.getWritePointer (0), inScratch.getWritePointer (1) };
    float* out[2] = { outL, outR };

    inputAccumulator += count * juce::jlimit (1.0 / maxSpeedRatio, (double) maxSpeedRatio, settings.speedRatio * timeScale);
    int nIn = (int) inputAccumulator;
    inputAccumulator -= nIn;
    nIn = juce::jmin (nIn, inCapacity);

    const bool wasFinished = reader.finished();
    reader.read (in[0], in[1], nIn);
    engine->stretch.process (in, nIn, out, count);

    if (reader.finished())
    {
        if (! wasFinished || tailRemaining < 0)
            tailRemaining = engine->outputLatency + (int) (engine->inputLatency / settings.speedRatio) + count;

        tailRemaining = juce::jmax (0, tailRemaining - count);
    }
}

float Voice::currentDive() const noexcept
{
    if (settings.diveSemitones == 0.0f)
        return 0.0f;

    const float t = (float) diveElapsed / juce::jmax (1.0f, settings.diveSeconds * (float) hostRate);
    if (t >= 1.0f)
        return 0.0f;

    const float e = 1.0f - t;
    return settings.diveSemitones * e * e;     // starts at the full amount, eases back to the normal pitch
}

/** Variable-speed read of the source stream (4-point Hermite), ratio gliding from ratioStart to ratioEnd. */
void Voice::renderResampled (float* outL, float* outR, int n, float ratioStart, float ratioEnd) noexcept
{
    const double slope = ((double) ratioEnd - (double) ratioStart) / (double) n;

    // how many new source frames are needed for this chunk
    double end = resampleFraction;
    for (int i = 0; i < n; ++i)
        end += (double) ratioStart + slope * (double) i;
    const int needed = juce::jlimit (0, resampleChunk * maxPitchRatio + 1, (int) std::floor (end));

    float* src[2] = { srcScratch.getWritePointer (0), srcScratch.getWritePointer (1) };
    for (int ch = 0; ch < 2; ++ch)
        for (int k = 0; k < 4; ++k)
            src[ch][k] = history[ch][k];

    const double average = 0.5 * ((double) ratioStart + (double) ratioEnd);
    produce (src[0] + 4, src[1] + 4, needed, 1.0 / juce::jmax (0.05, average));

    double position = 1.0 + resampleFraction;       // interpolates between src[1] and src[2] at the start
    const int lastIndex = needed + 1;
    for (int i = 0; i < n; ++i)
    {
        const int index = juce::jmin (lastIndex, (int) position);
        const auto f = (float) (position - (double) index);

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* s = src[ch] + index;
            const float ym1 = s[-1], y0 = s[0], y1 = s[1], y2 = s[2];
            const float c1 = 0.5f * (y1 - ym1);
            const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
            const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
            (ch == 0 ? outL : outR)[i] = ((c3 * f + c2) * f + c1) * f + y0;
        }
        position += (double) ratioStart + slope * (double) i;
    }

    for (int ch = 0; ch < 2; ++ch)
        for (int k = 0; k < 4; ++k)
            history[ch][k] = src[ch][needed + k];

    resampleFraction = juce::jlimit (0.0, 1.0, end - (double) needed);
    resampling = true;
}

void Voice::render (float* left, float* right, int numSamples, float pitchSemitones, float formantSemitones) noexcept
{
    if (! bypass && formantSemitones != lastFormant)
    {
        engine->stretch.setFormantSemitones (formantSemitones);
        lastFormant = formantSemitones;
    }

    int done = 0;

    while (active && done < numSamples)
    {
        const float semitones = juce::jlimit (-36.0f, 36.0f, pitchSemitones + currentDive());
        const float ratio = semitones == 0.0f ? 1.0f : std::pow (2.0f, semitones / 12.0f);
        const bool moving = std::abs (ratio - 1.0f) > 1.0e-5f || std::abs (previousRatio - 1.0f) > 1.0e-5f;
        const bool resample = moving || resampling;

        const int n = juce::jmin (resample ? resampleChunk : chunkSize, numSamples - done);
        float* tl = outScratch.getWritePointer (0);
        float* tr = outScratch.getWritePointer (1);

        if (resample)
        {
            renderResampled (tl, tr, n, previousRatio, ratio);
        }
        else
        {
            produce (tl, tr, n, 1.0);

            // remember the last frames so a later pitch movement can start without a gap
            for (int k = 0; k < 4; ++k)
            {
                const int from = n - 4 + k;
                history[0][k] = from >= 0 ? tl[from] : history[0][juce::jmin (3, k + n)];
                history[1][k] = from >= 0 ? tr[from] : history[1][juce::jmin (3, k + n)];
            }
        }

        previousRatio = ratio;
        diveElapsed += n;

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
