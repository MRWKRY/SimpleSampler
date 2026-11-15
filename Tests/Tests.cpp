// SimpleSampler automated tests (plain console app, exit code 0 = all passed)

#include "PluginProcessor.h"
#include "ParamMath.h"
#if SS_HAS_PRESETS
 #include "PresetManager.h"
#endif
#include <iostream>

using namespace ParamMath;

namespace
{
    int failures = 0, checks = 0;

    void check (bool ok, const juce::String& what)
    {
        ++checks;
        if (! ok)
        {
            ++failures;
            std::cout << "  FAIL: " << what << std::endl;
        }
    }

    void checkNear (double actual, double expected, double tolerance, const juce::String& what)
    {
        check (std::abs (actual - expected) <= tolerance,
               what + " (expected " + juce::String (expected, 4) + " +-" + juce::String (tolerance, 4)
                    + ", got " + juce::String (actual, 4) + ")");
    }

    void section (const char* name) { std::cout << "[" << name << "]" << std::endl; }

    juce::File tempDir()
    {
        auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SimpleSamplerTests");
        d.createDirectory();
        return d;
    }

    /** Writes a sine test file. */
    juce::File writeSine (const juce::String& name, double sampleRate, double seconds, double freq, int channels)
    {
        auto file = tempDir().getChildFile (name);
        file.deleteFile();

        const int n = (int) (sampleRate * seconds);
        juce::AudioBuffer<float> buffer (channels, n);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < n; ++i)
                buffer.setSample (ch, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sampleRate));

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file),
                                                                              sampleRate, (unsigned int) channels, 16, {}, 0));
        jassert (writer != nullptr);
        writer->writeFromAudioSampleBuffer (buffer, 0, n);
        writer.reset();
        return file;
    }

    void setParam (SimpleSamplerProcessor& p, int pad, const char* name, float value)
    {
        auto* param = p.getPadParameter (pad, name);
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    float getParam (SimpleSamplerProcessor& p, int pad, const char* name)
    {
        return p.apvts.getRawParameterValue (id::pad (pad, name))->load();
    }

    struct Event { int block; juce::MidiMessage message; };

    /** Renders `seconds` of audio, inserting the given MIDI events at the start of their block. */
    juce::AudioBuffer<float> render (SimpleSamplerProcessor& p, double sampleRate, double seconds,
                                     const std::vector<Event>& events, int blockSize = 512)
    {
        const int total = (int) (seconds * sampleRate);
        juce::AudioBuffer<float> out (2, total);
        out.clear();
        juce::AudioBuffer<float> block (2, blockSize);

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
        }
        return out;
    }

    /** Time of the last sample louder than the threshold, in seconds. */
    double soundingLength (const juce::AudioBuffer<float>& b, double sampleRate, float threshold = 0.02f)
    {
        const float* d = b.getReadPointer (0);
        for (int i = b.getNumSamples(); --i >= 0;)
            if (std::abs (d[i]) > threshold)
                return (i + 1) / sampleRate;
        return 0.0;
    }

    /** Frequency estimated from zero crossings between two times. */
    double frequency (const juce::AudioBuffer<float>& b, double sampleRate, double from, double to)
    {
        const float* d = b.getReadPointer (0);
        const int a = (int) (from * sampleRate), e = juce::jmin (b.getNumSamples() - 1, (int) (to * sampleRate));
        int crossings = 0;
        for (int i = a; i < e; ++i)
            if (d[i] <= 0.0f && d[i + 1] > 0.0f)
                ++crossings;
        return crossings / ((e - a) / sampleRate);
    }

    float peak (const juce::AudioBuffer<float>& b, double sampleRate, double from, double to)
    {
        const int a = juce::jlimit (0, b.getNumSamples(), (int) (from * sampleRate));
        const int e = juce::jlimit (a, b.getNumSamples(), (int) (to * sampleRate));
        return b.getMagnitude (0, a, e - a);
    }

    float maxStep (const juce::AudioBuffer<float>& b, double sampleRate, double from, double to)
    {
        const float* d = b.getReadPointer (0);
        const int a = (int) (from * sampleRate), e = juce::jmin (b.getNumSamples() - 1, (int) (to * sampleRate));
        float m = 0.0f;
        for (int i = a; i < e; ++i)
            m = juce::jmax (m, std::abs (d[i + 1] - d[i]));
        return m;
    }

    bool waitForLoads (SimpleSamplerProcessor& p, int timeoutMs = 8000)
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

    const auto noteOn  = [] (int note) { return juce::MidiMessage::noteOn (1, note, (juce::uint8) 127); };
    const auto noteOff = [] (int note) { return juce::MidiMessage::noteOff (1, note); };
}

//==============================================================================
static void testMath()
{
    section ("BPM / pitch maths and linked inputs");

    checkNear (stretchRatio (123.0, 139.0), 139.0 / 123.0, 1e-9, "ratio 123 -> 139");
    checkNear (stretchRatio (120.0, 120.0), 1.0, 1e-12, "same BPM keeps the original speed");
    checkNear (stretchRatio (100.0, 200.0), 2.0, 1e-12, "ratio 100 -> 200");
    checkNear (stretchRatio (0.0, 120.0), 1.0, 1e-12, "invalid start BPM is ignored");

    checkNear (resultBpmFromDiff (123.0, 16.0), 139.0, 1e-9, "diff +16 -> result 139");
    checkNear (diffFromResultBpm (123.0, 139.0), 16.0, 1e-9, "result 139 -> diff +16");
    checkNear (resultBpmFromDiff (100.0, 30.0), 130.0, 1e-9, "diff +30 -> result 130");
    checkNear (diffFromResultBpm (100.0, resultBpmFromDiff (100.0, -25.5)), -25.5, 1e-9, "diff round trip");
    checkNear (resultBpmFromDiff (390.0, 100.0), maxBpm, 1e-9, "result is clamped to the BPM range");

    check (semitonesFromNotes (60, 61) == 1, "C3 -> C#3 = +1 st");
    check (semitonesFromNotes (60, 48) == -12, "C3 -> C2 = -12 st");
    check (semitonesFromNotes (60, 100) == 24, "shift is clamped to +24");
    check (resultNoteFromSemitones (60, 1) == 61, "+1 st from C3 = C#3");
    check (resultNoteFromSemitones (60, semitonesFromNotes (60, 67)) == 67, "note round trip");
    checkNear (pitchRatio (12.0), 2.0, 1e-9, "+12 st doubles the frequency");

    check (noteName (36) == "C1", "36 is C1");
    check (noteName (60) == "C3", "60 is C3");
    check (noteName (61) == "C#3", "61 is C#3");
    check (parseNote ("C1") == 36, "parse C1");
    check (parseNote ("c#3") == 61, "parse c#3");
    check (parseNote ("Db3") == 61, "parse Db3");
    check (parseNote ("B2") == 59, "parse B2");
    check (parseNote ("Bb2") == 58, "parse Bb2");
    check (parseNote ("C-2") == 0, "parse C-2");
    check (parseNote ("64") == 64, "parse plain number");
    check (parseNote ("H3") < 0, "reject invalid name");
    for (int n = 0; n < 128; ++n)
        if (parseNote (noteName (n)) != n) { check (false, "note name round trip " + juce::String (n)); break; }

    check (signedBpmText (16.0) == "+16", "diff text +16");
    check (signedBpmText (-2.5) == "-2.5", "diff text -2.5");
    check (bpmText (123.0) == "123", "bpm text");
}

//==============================================================================
static void testEngine()
{
    section ("audio engine (rendered offline)");

    const double hostRate = 48000.0;
    // 44.1 kHz mono file on a 48 kHz host: also exercises sample-rate conversion and mono handling
    const auto sine = writeSine ("sine440.wav", 44100.0, 1.0, 440.0, 1);

    SimpleSamplerProcessor p;
    p.setPlayConfigDetails (0, 2, hostRate, 512);
    p.prepareToPlay (hostRate, 512);
    check (p.loadSampleIntoPadBlocking (0, sine), "load test sample");

    {
        auto s = p.getPad (0).getSample();
        check (s != nullptr && std::abs (s->sampleRate - hostRate) < 1.0, "sample converted to the host rate");
        if (s != nullptr)
            checkNear (s->lengthSeconds, 1.0, 0.002, "converted length");
    }

    auto reset = [&]
    {
        setParam (p, 0, id::srcBpm, 120.0f);  setParam (p, 0, id::dstBpm, 120.0f);
        setParam (p, 0, id::semitones, 0.0f); setParam (p, 0, id::loop, 0.0f);
        setParam (p, 0, id::loopStart, 0.0f); setParam (p, 0, id::loopEnd, 1.0f);
        setParam (p, 0, id::attack, 1.0f);    setParam (p, 0, id::decay, 100.0f);
        setParam (p, 0, id::sustain, 1.0f);   setParam (p, 0, id::release, 20.0f);
        render (p, hostRate, 0.3, { { 0, juce::MidiMessage::allSoundOff (1) } });
    };

    // --- untouched playback
    reset();
    {
        auto out = render (p, hostRate, 1.5, { { 0, noteOn (36) } });
        checkNear (soundingLength (out, hostRate), 1.0, 0.01, "original: length");
        checkNear (frequency (out, hostRate, 0.2, 0.8), 440.0, 3.0, "original: pitch");
        check (peak (out, hostRate, 0.0, 0.01) > 0.2f, "original: starts immediately");
    }

    // --- one-shot ignores note-off
    reset();
    {
        auto out = render (p, hostRate, 1.5, { { 0, noteOn (36) }, { 2, noteOff (36) } });
        checkNear (soundingLength (out, hostRate), 1.0, 0.01, "one-shot keeps playing after note-off");
    }

    // --- BPM only: 100 -> 200 = twice as fast, same pitch
    reset();
    setParam (p, 0, id::srcBpm, 100.0f); setParam (p, 0, id::dstBpm, 200.0f);
    {
        auto out = render (p, hostRate, 1.5, { { 0, noteOn (36) } });
        checkNear (soundingLength (out, hostRate), 0.5, 0.05, "BPM x2: length halves");
        checkNear (frequency (out, hostRate, 0.1, 0.4), 440.0, 6.0, "BPM x2: pitch unchanged");
        check (peak (out, hostRate, 0.0, 0.02) > 0.1f, "BPM x2: starts without latency");
    }

    // --- BPM only: 123 -> 139
    reset();
    setParam (p, 0, id::srcBpm, 123.0f); setParam (p, 0, id::dstBpm, 139.0f);
    {
        auto out = render (p, hostRate, 1.5, { { 0, noteOn (36) } });
        checkNear (soundingLength (out, hostRate), 123.0 / 139.0, 0.05, "BPM 123->139: length");
        checkNear (frequency (out, hostRate, 0.2, 0.7), 440.0, 6.0, "BPM 123->139: pitch unchanged");
    }

    // --- BPM slower: 120 -> 60
    reset();
    setParam (p, 0, id::srcBpm, 120.0f); setParam (p, 0, id::dstBpm, 60.0f);
    {
        auto out = render (p, hostRate, 2.6, { { 0, noteOn (36) } });
        checkNear (soundingLength (out, hostRate), 2.0, 0.08, "BPM x0.5: length doubles");
        checkNear (frequency (out, hostRate, 0.4, 1.6), 440.0, 6.0, "BPM x0.5: pitch unchanged");
    }

    // --- pitch only: +12 st, same length
    reset();
    setParam (p, 0, id::semitones, 12.0f);
    {
        auto out = render (p, hostRate, 1.5, { { 0, noteOn (36) } });
        checkNear (soundingLength (out, hostRate), 1.0, 0.05, "pitch +12: length unchanged");
        checkNear (frequency (out, hostRate, 0.2, 0.8), 880.0, 12.0, "pitch +12: one octave up");
    }

    // --- pitch only: +1 st
    reset();
    setParam (p, 0, id::semitones, 1.0f);
    {
        auto out = render (p, hostRate, 1.5, { { 0, noteOn (36) } });
        checkNear (soundingLength (out, hostRate), 1.0, 0.05, "pitch +1: length unchanged");
        checkNear (frequency (out, hostRate, 0.2, 0.8), 440.0 * pitchRatio (1.0), 6.0, "pitch +1: one semitone up");
    }

    // --- both at once, independent
    reset();
    setParam (p, 0, id::srcBpm, 120.0f); setParam (p, 0, id::dstBpm, 150.0f);
    setParam (p, 0, id::semitones, -5.0f);
    {
        auto out = render (p, hostRate, 1.5, { { 0, noteOn (36) } });
        checkNear (soundingLength (out, hostRate), 0.8, 0.05, "BPM+pitch: length follows BPM only");
        checkNear (frequency (out, hostRate, 0.2, 0.6), 440.0 * pitchRatio (-5.0), 6.0, "BPM+pitch: pitch follows semitones only");
    }

    // --- loop: keeps going, stops on note-off, no click at the seam
    for (int mode = 0; mode < 3; ++mode)
    {
        reset();
        setParam (p, 0, id::loop, 1.0f);
        // mode 0: loop inside the sample (pre-roll fade), 1: from the very start (post-roll fade), 2: whole file (dip)
        setParam (p, 0, id::loopStart, mode == 0 ? 0.313f : 0.0f);
        setParam (p, 0, id::loopEnd,   mode == 2 ? 1.0f : 0.771f);
        const auto label = "loop mode " + juce::String (mode) + ": ";

        const int offBlock = (int) (3.0 * hostRate / 512);
        auto out = render (p, hostRate, 3.5, { { 0, noteOn (36) }, { offBlock, noteOff (36) } });
        check (peak (out, hostRate, 2.5, 2.9) > 0.3f, label + "still sounding after 2.5 s");
        check (peak (out, hostRate, 3.2, 3.5) < 0.001f, label + "stops after note-off + release");
        // a 440 Hz sine at 0.5 changes by at most ~0.029 per sample; a hard cut would jump far more
        check (maxStep (out, hostRate, 0.0, 2.9) < 0.07f, label + "no click at the loop seam (max step "
                                                            + juce::String (maxStep (out, hostRate, 0.0, 2.9), 4) + ")");
    }

    // --- loop together with stretch
    reset();
    setParam (p, 0, id::loop, 1.0f); setParam (p, 0, id::loopStart, 0.25f); setParam (p, 0, id::loopEnd, 0.75f);
    setParam (p, 0, id::srcBpm, 100.0f); setParam (p, 0, id::dstBpm, 130.0f);
    {
        const int offBlock = (int) (2.0 * hostRate / 512);
        auto out = render (p, hostRate, 2.6, { { 0, noteOn (36) }, { offBlock, noteOff (36) } });
        check (peak (out, hostRate, 1.5, 1.9) > 0.2f, "stretched loop keeps sounding");
        check (peak (out, hostRate, 2.3, 2.6) < 0.001f, "stretched loop stops after note-off");
        checkNear (frequency (out, hostRate, 0.5, 1.8), 440.0, 8.0, "stretched loop: pitch unchanged");
    }

    // --- envelope: long attack starts quietly
    reset();
    setParam (p, 0, id::attack, 500.0f);
    {
        auto out = render (p, hostRate, 1.2, { { 0, noteOn (36) } });
        check (peak (out, hostRate, 0.0, 0.02) < 0.05f, "attack: quiet at the start");
        check (peak (out, hostRate, 0.6, 0.9) > 0.4f, "attack: full level later");
    }

    // --- envelope: sustain level
    reset();
    setParam (p, 0, id::decay, 50.0f); setParam (p, 0, id::sustain, 0.25f);
    {
        auto out = render (p, hostRate, 1.2, { { 0, noteOn (36) } });
        checkNear (peak (out, hostRate, 0.5, 0.9), 0.5 * 0.25, 0.02, "sustain level 25%");
    }

    // --- polyphony: retriggering layers voices, capped at 32
    reset();
    {
        std::vector<Event> events;
        for (int i = 0; i < 4; ++i) events.push_back ({ i * 4, noteOn (36) });
        auto out = render (p, hostRate, 0.4, events);
        check (p.getActiveVoiceCount() == 4, "retriggered pad layers voices (got " + juce::String (p.getActiveVoiceCount()) + ")");
        check (p.getPad (0).activeVoices.load() == 4, "pad reports its voices");
    }
    reset();
    {
        std::vector<Event> events;
        for (int i = 0; i < 50; ++i) events.push_back ({ i, noteOn (36) });
        render (p, hostRate, 0.7, events);
        check (p.getActiveVoiceCount() == 32, "voice limit of 32 (got " + juce::String (p.getActiveVoiceCount()) + ")");
    }

    // --- note mapping
    reset();
    {
        auto out = render (p, hostRate, 0.3, { { 0, noteOn (37) } });
        check (peak (out, hostRate, 0.0, 0.3) < 1.0e-6f, "other notes do not trigger pad 1");
        setParam (p, 0, id::note, 61.0f);
        out = render (p, hostRate, 0.3, { { 0, noteOn (61) } });
        check (peak (out, hostRate, 0.0, 0.3) > 0.3f, "changed note number triggers the pad");
        setParam (p, 0, id::note, 36.0f);
    }

    // --- pad click from the editor
    reset();
    {
        p.triggerPadFromUI (0, true);
        auto out = render (p, hostRate, 0.3, {});
        check (peak (out, hostRate, 0.0, 0.3) > 0.2f, "clicking a pad plays it");
    }

    p.releaseResources();
}

//==============================================================================
static void testState()
{
    section ("state save / restore");

    const auto sine  = writeSine ("stateA.wav", 48000.0, 0.25, 220.0, 2);
    const auto sine2 = writeSine ("stateB.wav", 44100.0, 0.25, 330.0, 1);

    juce::MemoryBlock saved;
    {
        SimpleSamplerProcessor a;
        a.prepareToPlay (48000.0, 256);
        check (a.loadSampleIntoPadBlocking (0, sine), "load A");
        check (a.loadSampleIntoPadBlocking (5, sine2), "load B");
        setParam (a, 0, id::srcBpm, 123.0f);  setParam (a, 0, id::dstBpm, 139.0f);
        setParam (a, 0, id::semitones, 1.0f); setParam (a, 0, id::srcPitch, 62.0f);
        setParam (a, 5, id::loop, 1.0f);      setParam (a, 5, id::loopStart, 0.2f);
        setParam (a, 5, id::loopEnd, 0.8f);   setParam (a, 5, id::note, 72.0f);
        setParam (a, 5, id::attack, 250.0f);  setParam (a, 5, id::sustain, 0.5f);
        a.setSelectedPad (5);
        a.setPresetName ("My Kit");
        a.getStateInformation (saved);
    }

    {
        SimpleSamplerProcessor b;
        b.prepareToPlay (48000.0, 256);
        b.setStateInformation (saved.getData(), (int) saved.getSize());
        check (waitForLoads (b), "samples reload in the background");

        checkNear (getParam (b, 0, id::srcBpm), 123.0, 1e-3, "restore start BPM");
        checkNear (getParam (b, 0, id::dstBpm), 139.0, 1e-3, "restore result BPM");
        checkNear (getParam (b, 0, id::semitones), 1.0, 1e-6, "restore semitones");
        checkNear (getParam (b, 0, id::srcPitch), 62.0, 1e-6, "restore start pitch");
        check (getParam (b, 5, id::loop) > 0.5f, "restore loop switch");
        checkNear (getParam (b, 5, id::loopStart), 0.2, 1e-4, "restore loop start");
        checkNear (getParam (b, 5, id::loopEnd), 0.8, 1e-4, "restore loop end");
        checkNear (getParam (b, 5, id::note), 72.0, 1e-6, "restore note number");
        checkNear (getParam (b, 5, id::attack), 250.0, 0.11, "restore attack");
        checkNear (getParam (b, 5, id::sustain), 0.5, 1e-3, "restore sustain");
        check (b.getSelectedPad() == 5, "restore selected pad");
        check (b.getPresetName() == "My Kit", "restore preset name");
        check (b.getPad (0).getFile() == sine, "restore sample path (pad 1)");
        check (b.getPad (5).getFile() == sine2, "restore sample path (pad 6)");
        check (b.getPad (0).getSample() != nullptr, "sample audio restored (pad 1)");
        check (b.getPad (5).getSample() != nullptr, "sample audio restored (pad 6)");
        check (! b.getPad (1).hasFile(), "empty pads stay empty");

        auto out = render (b, 48000.0, 0.4, { { 0, noteOn (72) } });
        check (peak (out, 48000.0, 0.0, 0.2) > 0.2f, "restored pad plays on its restored note");
    }

    // moved sample: the absolute path is gone, the file name is found next to the preset
    {
        SimpleSamplerProcessor c;
        c.prepareToPlay (48000.0, 256);
        check (c.loadSampleIntoPadBlocking (2, sine), "load for move test");
        auto tree = c.createStateTree();

        auto movedDir = tempDir().getChildFile ("moved");
        movedDir.deleteRecursively();
        movedDir.createDirectory();
        const auto moved = movedDir.getChildFile (sine.getFileName());
        check (sine.copyFileTo (moved), "copy sample");

        auto padsTree = tree.getChildWithName ("Pads");
        check (padsTree.getNumChildren() == 1, "one pad stored");
        padsTree.getChild (0).setProperty ("path", "Q:\\does\\not\\exist\\" + sine.getFileName(), nullptr);

        SimpleSamplerProcessor d;
        d.prepareToPlay (48000.0, 256);
        check (d.restoreStateTree (tree, movedDir), "restore with moved sample");
        check (waitForLoads (d), "moved sample loads");
        check (d.getPad (2).getFile() == moved, "moved sample found by file name");
        check (d.getPad (2).getSample() != nullptr && ! d.getPad (2).isMissing(), "moved sample is playable");

        SimpleSamplerProcessor e;
        e.prepareToPlay (48000.0, 256);
        check (e.restoreStateTree (tree, {}), "restore with missing sample");
        check (e.getPad (2).isMissing() && e.getPad (2).getFileName() == sine.getFileName(), "missing sample keeps its name");
        check (e.createStateTree().getChildWithName ("Pads").getNumChildren() == 1, "missing sample survives a re-save");
    }
}

//==============================================================================
#if SS_HAS_PRESETS
static void testPresets()
{
    section ("presets");

    auto dir = tempDir().getChildFile ("presets");
    dir.deleteRecursively();

    const auto sine = writeSine ("presetA.wav", 48000.0, 0.25, 220.0, 2);

    SimpleSamplerProcessor p;
    p.prepareToPlay (48000.0, 256);
    PresetManager presets (p, dir);

    check (presets.getPresetNames().isEmpty(), "no presets at first");

    check (p.loadSampleIntoPadBlocking (3, sine), "load sample");
    setParam (p, 3, id::srcBpm, 90.0f); setParam (p, 3, id::dstBpm, 120.0f);
    setParam (p, 3, id::semitones, -3.0f);
    check (presets.savePreset ("Kit A"), "save Kit A");

    p.clearPad (3);
    setParam (p, 3, id::srcBpm, 150.0f); setParam (p, 3, id::dstBpm, 150.0f);
    setParam (p, 3, id::semitones, 7.0f);
    check (presets.savePreset ("Kit B"), "save Kit B");

    auto names = presets.getPresetNames();
    check (names.size() == 2 && names[0] == "Kit A" && names[1] == "Kit B", "preset list");

    check (presets.loadPreset ("Kit A"), "load Kit A");
    check (waitForLoads (p), "preset samples load");
    checkNear (getParam (p, 3, id::srcBpm), 90.0, 1e-3, "Kit A start BPM");
    checkNear (getParam (p, 3, id::dstBpm), 120.0, 1e-3, "Kit A result BPM");
    checkNear (getParam (p, 3, id::semitones), -3.0, 1e-6, "Kit A semitones");
    check (p.getPad (3).getFile() == sine && p.getPad (3).getSample() != nullptr, "Kit A sample");
    check (p.getPresetName() == "Kit A", "current preset name");

    check (presets.loadNext(), "next preset");
    check (p.getPresetName() == "Kit B", "next is Kit B");
    checkNear (getParam (p, 3, id::semitones), 7.0, 1e-6, "Kit B semitones");
    check (! p.getPad (3).hasFile(), "Kit B has no sample on pad 4");

    check (presets.loadNext(), "next wraps around");
    check (p.getPresetName() == "Kit A", "wrapped to Kit A");
    check (presets.loadPrevious(), "previous preset");
    check (p.getPresetName() == "Kit B", "previous is Kit B");

    check (presets.deletePreset ("Kit B"), "delete preset");
    check (presets.getPresetNames().size() == 1, "one preset left");
    check (! presets.loadPreset ("Nope"), "loading an unknown preset fails");
    check (presets.savePreset ("bad:/name?") && presets.getPresetNames().size() == 2, "illegal characters are cleaned");
}
#endif

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    testMath();
    testEngine();
    testState();
   #if SS_HAS_PRESETS
    testPresets();
   #endif

    tempDir().deleteRecursively();

    std::cout << std::endl << checks << " checks, " << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}
