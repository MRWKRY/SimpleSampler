#pragma once

// Shared helpers for the SimpleSampler test executable.

#include "PluginProcessor.h"
#include "ParamMath.h"
#include <iostream>

inline int failures = 0, checks = 0;
inline bool verbose = false;

inline void check (bool ok, const juce::String& what)
{
    ++checks;
    if (verbose)
        std::cout << "  " << (ok ? "ok  : " : "FAIL: ") << what << std::endl;
    else if (! ok)
        std::cout << "  FAIL: " << what << std::endl;
    if (! ok)
        ++failures;
}

inline void checkNear (double actual, double expected, double tolerance, const juce::String& what)
{
    check (std::abs (actual - expected) <= tolerance,
           what + " (expected " + juce::String (expected, 4) + " +-" + juce::String (tolerance, 4)
                + ", got " + juce::String (actual, 4) + ")");
}

inline void section (const char* name) { std::cout << "[" << name << "]" << std::endl; }

inline juce::File tempDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SimpleSamplerTests");
    d.createDirectory();
    return d;
}

inline juce::File writeBuffer (const juce::String& name, const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    auto file = tempDir().getChildFile (name);
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), sampleRate,
                                                                          (unsigned int) buffer.getNumChannels(), 24, {}, 0));
    jassert (writer != nullptr);
    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    writer.reset();
    return file;
}

/** Writes a sine test file (amplitude 0.5). */
inline juce::File writeSine (const juce::String& name, double sampleRate, double seconds, double freq, int channels)
{
    const int n = (int) (sampleRate * seconds);
    juce::AudioBuffer<float> buffer (channels, n);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < n; ++i)
            buffer.setSample (ch, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sampleRate));
    return writeBuffer (name, buffer, sampleRate);
}

inline void setParam (SimpleSamplerProcessor& p, const juce::String& paramID, float value)
{
    auto* param = p.apvts.getParameter (paramID);
    jassert (param != nullptr);
    if (param != nullptr)
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

inline void setParam (SimpleSamplerProcessor& p, int pad, const char* name, float value)
{
    setParam (p, ParamMath::id::pad (pad, name), value);
}

inline float getParam (SimpleSamplerProcessor& p, const juce::String& paramID)
{
    auto* v = p.apvts.getRawParameterValue (paramID);
    jassert (v != nullptr);
    return v != nullptr ? v->load() : 0.0f;
}

inline float getParam (SimpleSamplerProcessor& p, int pad, const char* name)
{
    return getParam (p, ParamMath::id::pad (pad, name));
}

/** Minimal host transport for tests. */
struct TestPlayHead : public juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setIsPlaying (playing);
        return info;
    }
};

struct Event { int block; juce::MidiMessage message; };

/** Renders `seconds` of audio, inserting the given MIDI events at the start of their block.
    If a play head is given it is attached and moved forward block by block. */
inline juce::AudioBuffer<float> render (SimpleSamplerProcessor& p, double sampleRate, double seconds,
                                        const std::vector<Event>& events, int blockSize = 512, TestPlayHead* playHead = nullptr)
{
    const int total = (int) (seconds * sampleRate);
    juce::AudioBuffer<float> out (2, total);
    out.clear();
    juce::AudioBuffer<float> block (2, blockSize);
    p.setPlayHead (playHead);

    int blockIndex = 0;
    for (int pos = 0; pos < total; pos += blockSize, ++blockIndex)
    {
        const int n = juce::jmin (blockSize, total - pos);
        juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, n);
        juce::MidiBuffer midi;
        for (auto& e : events)
            if (e.block == blockIndex)
                midi.addEvent (e.message, 0);

        p.processBlock (view, midi);
        out.copyFrom (0, pos, view, 0, 0, n);
        out.copyFrom (1, pos, view, 1, 0, n);

        if (playHead != nullptr && playHead->playing)
            playHead->ppq += (double) n * playHead->bpm / (60.0 * sampleRate);
    }

    p.setPlayHead (nullptr);
    return out;
}

/** Time of the last sample louder than the threshold, in seconds. */
inline double soundingLength (const juce::AudioBuffer<float>& b, double sampleRate, float threshold = 0.02f)
{
    const float* d = b.getReadPointer (0);
    for (int i = b.getNumSamples(); --i >= 0;)
        if (std::abs (d[i]) > threshold)
            return (i + 1) / sampleRate;
    return 0.0;
}

/** Frequency estimated from zero crossings between two times. */
inline double frequency (const juce::AudioBuffer<float>& b, double sampleRate, double from, double to, int channel = 0)
{
    const float* d = b.getReadPointer (channel);
    const int a = (int) (from * sampleRate), e = juce::jmin (b.getNumSamples() - 1, (int) (to * sampleRate));
    int crossings = 0;
    for (int i = a; i < e; ++i)
        if (d[i] <= 0.0f && d[i + 1] > 0.0f)
            ++crossings;
    return crossings / ((e - a) / sampleRate);
}

inline float peak (const juce::AudioBuffer<float>& b, double sampleRate, double from, double to, int channel = 0)
{
    const int a = juce::jlimit (0, b.getNumSamples(), (int) (from * sampleRate));
    const int e = juce::jlimit (a, b.getNumSamples(), (int) (to * sampleRate));
    return b.getMagnitude (channel, a, e - a);
}

inline float rms (const juce::AudioBuffer<float>& b, double sampleRate, double from, double to, int channel = 0)
{
    const int a = juce::jlimit (0, b.getNumSamples(), (int) (from * sampleRate));
    const int e = juce::jlimit (a, b.getNumSamples(), (int) (to * sampleRate));
    return e > a ? b.getRMSLevel (channel, a, e - a) : 0.0f;
}

inline float maxStep (const juce::AudioBuffer<float>& b, double sampleRate, double from, double to)
{
    const float* d = b.getReadPointer (0);
    const int a = (int) (from * sampleRate), e = juce::jmin (b.getNumSamples() - 1, (int) (to * sampleRate));
    float m = 0.0f;
    for (int i = a; i < e; ++i)
        m = juce::jmax (m, std::abs (d[i + 1] - d[i]));
    return m;
}

inline bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        const float* d = b.getReadPointer (ch);
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (d[i]))
                return false;
    }
    return true;
}

/** Largest absolute difference between two renders. */
inline float maxDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    float m = 0.0f;
    const int n = juce::jmin (a.getNumSamples(), b.getNumSamples());
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
            m = juce::jmax (m, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    return m;
}

inline bool waitForLoads (SimpleSamplerProcessor& p, int timeoutMs = 8000)
{
    const auto start = juce::Time::getMillisecondCounter();
    for (;;)
    {
        bool busy = false;
        for (int i = 0; i < SimpleSamplerProcessor::numPads; ++i)
            busy = busy || p.getPad (i).loading.load();
        if (! busy) return true;
        if (juce::Time::getMillisecondCounter() - start > (juce::uint32) timeoutMs) return false;
        juce::Thread::sleep (10);
    }
}

inline juce::MidiMessage noteOn (int note)  { return juce::MidiMessage::noteOn (1, note, (juce::uint8) 127); }
inline juce::MidiMessage noteOff (int note) { return juce::MidiMessage::noteOff (1, note); }

/** Tests of everything added in version 2 (TestsV2.cpp). */
void runV2Tests();
