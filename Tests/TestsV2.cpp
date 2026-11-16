// Tests for version 2: mixer / tone, send effects, genre knobs, modulation, randomiser, state compatibility.

#include "TestHelpers.h"
#include "Modulation/TempoSync.h"
#if SS_HAS_PRESETS
 #include "PresetManager.h"
#endif
#include <set>

using namespace ParamMath;

namespace
{
    constexpr double sr = 48000.0;

    /** Processor with a 1 s / 440 Hz stereo sine on pad 1 and a 0.1 s burst on pad 2 (both already at the host rate). */
    struct Rig
    {
        SimpleSamplerProcessor p;
        juce::File sine, burst;

        Rig()
        {
            sine  = writeSine ("v2_sine.wav", sr, 1.0, 440.0, 2);
            burst = writeSine ("v2_burst.wav", sr, 0.1, 440.0, 2);
            p.prepareToPlay (sr, 512);
            check (p.loadSampleIntoPadBlocking (0, sine), "load sine");
            check (p.loadSampleIntoPadBlocking (1, burst), "load burst");
        }

        /** Every parameter back to default, no modulation, all voices and effect tails gone. Samples stay. */
        void reset()
        {
            for (auto* param : p.getParameters())
                param->setValueNotifyingHost (param->getDefaultValue());
            p.getModMatrix().fromTree ({});
            p.releaseResources();
            p.setSelectedPad (0);
        }

        juce::AudioBuffer<float> play (double seconds, int note = 36, TestPlayHead* playHead = nullptr)
        {
            return render (p, sr, seconds, { { 0, noteOn (note) } }, 512, playHead);
        }
    };

    juce::String padId (int pad, const char* name) { return id::pad (pad, name); }
}

//==============================================================================
static void testTempoMaths()
{
    section ("tempo sync maths");

    using namespace TempoSync;
    checkNear (divisionBeats (divQuarter), 1.0, 1e-12, "1/4 = 1 beat");
    checkNear (divisionBeats (divEighth), 0.5, 1e-12, "1/8 = half a beat");
    checkNear (divisionBeats (divEighthDotted), 0.75, 1e-12, "1/8. = 0.75 beat");
    checkNear (divisionBeats (11), 1.0 / 3.0, 1e-12, "1/8T = a third of a beat");
    checkNear (divisionBeats (0), 16.0, 1e-12, "4 bars = 16 beats");
    checkNear (divisionBeats (17), 1.0 / 12.0, 1e-12, "1/32T");
    check (divisionNames().size() == numDivisions, "one name per division");
    check (juce::String (divisions()[divEighthDotted].name) == "1/8.", "division name");

    checkNear (divisionMs (divQuarter, 120.0), 500.0, 1e-9, "1/4 at 120 BPM = 500 ms");
    checkNear (divisionMs (divEighthDotted, 120.0), 375.0, 1e-9, "1/8. at 120 BPM = 375 ms");
    checkNear (divisionMs (divSixteenth, 150.0), 100.0, 1e-9, "1/16 at 150 BPM = 100 ms");
    checkNear (divisionMs (11, 90.0), 60000.0 / 90.0 / 3.0, 1e-9, "1/8T at 90 BPM");

    // one rate unit per division: after 1 second at 120 BPM a 1/4 LFO has done 2 cycles
    checkNear (unitsPerSample (true, divQuarter, 0.0, 120.0, 48000.0) * 48000.0, 2.0, 1e-9, "synced rate 1/4 @120 = 2 Hz");
    checkNear (unitsPerSample (true, divBar, 0.0, 60.0, 48000.0) * 48000.0, 0.25, 1e-9, "synced rate 1 bar @60 = 0.25 Hz");
    checkNear (unitsPerSample (false, divQuarter, 3.5, 120.0, 48000.0) * 48000.0, 3.5, 1e-9, "free rate in Hz");
    checkNear (unitsFromBeats (6.0, divBar), 1.5, 1e-12, "beat 6 is half-way through bar 2");

    // shapes
    checkNear (shapeValue (sine, 0.25, nullptr, 1), 1.0, 1e-6, "sine peak at a quarter cycle");
    checkNear (shapeValue (sine, 7.75, nullptr, 1), -1.0, 1e-6, "sine trough");
    checkNear (shapeValue (triangle, 0.25, nullptr, 1), 1.0, 1e-6, "triangle peak");
    checkNear (shapeValue (triangle, 0.5, nullptr, 1), 0.0, 1e-6, "triangle zero crossing");
    checkNear (shapeValue (saw, 0.0, nullptr, 1), 1.0, 1e-6, "saw starts high");
    checkNear (shapeValue (saw, 0.75, nullptr, 1), -0.5, 1e-6, "saw falls");
    check (shapeValue (square, 0.2, nullptr, 1) == 1.0f && shapeValue (square, 0.7, nullptr, 1) == -1.0f, "square");

    check (shapeValue (sampleHold, 3.1, nullptr, 5) == shapeValue (sampleHold, 3.9, nullptr, 5), "S&H holds inside a step");
    check (shapeValue (sampleHold, 3.1, nullptr, 5) != shapeValue (sampleHold, 4.1, nullptr, 5), "S&H changes on the next step");
    check (shapeValue (sampleHold, 3.1, nullptr, 5) != shapeValue (sampleHold, 3.1, nullptr, 6), "each modulator has its own random sequence");
    checkNear (shapeValue (smoothRandom, 4.0, nullptr, 5), shapeValue (sampleHold, 4.0, nullptr, 5), 1e-6, "smooth random passes through the random points");
    {
        bool bounded = true; float biggestJump = 0.0f, previous = shapeValue (smoothRandom, 0.0, nullptr, 9);
        for (int i = 1; i < 4000; ++i)
        {
            const float v = shapeValue (smoothRandom, i * 0.005, nullptr, 9);
            bounded = bounded && v >= -1.0f && v <= 1.0f;
            biggestJump = juce::jmax (biggestJump, std::abs (v - previous));
            previous = v;
        }
        check (bounded, "smooth random stays in -1..1");
        check (biggestJump < 0.05f, "smooth random has no jumps");
    }

    float steps[numSteps];
    for (int i = 0; i < numSteps; ++i) steps[i] = (float) i / 15.0f;
    checkNear (shapeValue (stepSeq, 0.5, steps, 1), 0.0, 1e-6, "step 1");
    checkNear (shapeValue (stepSeq, 15.2, steps, 1), 1.0, 1e-6, "step 16");
    checkNear (shapeValue (stepSeq, 16.2, steps, 1), 0.0, 1e-6, "steps wrap after 16");
    checkNear (shapeValue (stepSeq, -0.5, steps, 1), 1.0, 1e-6, "steps wrap for negative positions");

    // pump / gate
    checkNear (pumpGain (0.0, 1.0f, 0.5f), 0.0, 1e-6, "pump: fully ducked on the beat");
    checkNear (pumpGain (0.5, 1.0f, 0.5f), 1.0, 1e-6, "pump: recovered after the release");
    checkNear (pumpGain (0.0, 0.4f, 0.5f), 0.6, 1e-6, "pump: depth follows the amount");
    check (pumpGain (0.1, 1.0f, 0.5f) < pumpGain (0.3, 1.0f, 0.5f), "pump: recovers gradually");
    check (gateStepIndex (0.0, 0.25) == 0 && gateStepIndex (0.3, 0.25) == 1 && gateStepIndex (4.0, 0.25) == 0, "gate step index");

    checkNear (intervalBeats (0), 1.0, 1e-12, "randomise interval: 1 beat");
    checkNear (intervalBeats (2), 4.0, 1e-12, "randomise interval: 1 bar");
}

//==============================================================================
static void testParameters()
{
    section ("parameters");

    SimpleSamplerProcessor p;
    const auto& params = p.getParameters();

    // 16 pads x (12 sampler + 21 tone/genre) + output + 14 FX + 23 genre settings + 4 x 21 modulator + 5
    check (params.size() == 655, "parameter count (got " + juce::String (params.size()) + ")");

    std::set<juce::String> ids, names;
    bool rangesOk = true, textOk = true, roundTripOk = true;
    for (auto* param : params)
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (ranged == nullptr) { rangesOk = false; continue; }

        ids.insert (ranged->paramID);
        names.insert (ranged->getName (64));

        const auto& range = ranged->getNormalisableRange();
        const float def = ranged->convertFrom0to1 (ranged->getDefaultValue());
        rangesOk = rangesOk && range.start < range.end && def >= range.start - 1e-4f && def <= range.end + 1e-4f;
        textOk = textOk && ranged->getName (64).isNotEmpty() && ranged->getText (ranged->getDefaultValue(), 32).isNotEmpty();

        for (float n : { 0.0f, 0.25f, 0.5f, 1.0f })
        {
            const float v = ranged->convertFrom0to1 (n);
            roundTripOk = roundTripOk && v >= range.start - 1e-3f && v <= range.end + 1e-3f
                          && std::abs (ranged->convertTo0to1 (v) - n) < 0.02f + (range.interval > 0 ? range.interval / (range.end - range.start) : 0.0f);
        }
    }
    check ((int) ids.size() == params.size(), "parameter IDs are unique");
    check ((int) names.size() == params.size(), "parameter names are unique");
    check (rangesOk, "every default lies inside its range");
    check (textOk, "every parameter has a name and a value text");
    check (roundTripOk, "normalised <-> real value round trip");

    check (p.apvts.getParameter (padId (0, id::level))->getName (64) == "Pad01 Level", "name: Pad01 Level");
    check (p.apvts.getParameter (padId (15, id::bloom))->getName (64) == "Pad16 Bloom", "name: Pad16 Bloom");
    check (p.apvts.getParameter (id::rvMix)->getName (64) == "Reverb Mix", "name: Reverb Mix");
    check (p.apvts.getParameter (padId (0, id::level))->getText (0.0f, 32) == "-inf dB", "Level text at minimum");
    checkNear (getParam (p, 0, id::level), 0.0, 1e-6, "Level default 0 dB");
    checkNear (getParam (p, id::tempo), 120.0, 1e-6, "manual tempo default 120");

    // host groups
    juce::StringArray groups;
    for (auto* g : p.getParameterTree().getSubgroups (false))
        groups.add (g->getName());
    check (groups.contains ("Pad 01") && groups.contains ("Pad 16") && groups.contains ("FX")
           && groups.contains ("Genre") && groups.contains ("Mod"), "parameter groups: " + groups.joinIntoString (", "));

    // modulation targets
    auto& matrix = p.getModMatrix();
    check (matrix.getNumTargets() == 16 * 21 + 12, "modulation targets (got " + juce::String (matrix.getNumTargets()) + ")");
    check (matrix.findTarget (padId (3, id::drive)) == p.getPadTarget (3, id::fxDrive), "target lookup");
    check (matrix.findTarget (padId (0, id::srcBpm)) < 0 && matrix.findTarget (padId (0, id::semitones)) < 0,
           "BPM and pitch conversion cannot be modulated");
    check (matrix.findTarget (id::master) < 0, "output volume is not a modulation target");
}

//==============================================================================
static void testMixer (Rig& rig)
{
    section ("pad mixer / tone");
    auto& p = rig.p;

    // ---- with every new control at its default the signal path must be exactly transparent
    rig.reset();
    {
        const auto out = rig.play (1.2);
        const auto sample = p.getPad (0).getSample();
        float worst = 0.0f;
        for (int i = (int) (0.01 * sr); i < (int) (0.98 * sr); ++i)
            worst = juce::jmax (worst, std::abs (out.getSample (0, i) - sample->buffer.getSample (0, i)),
                                       std::abs (out.getSample (1, i) - sample->buffer.getSample (1, i)));
        check (worst < 2.0e-5f, "defaults are bit-transparent (max error " + juce::String (worst, 8) + ")");
        check (peak (out, sr, 1.05, 1.2) < 1.0e-6f, "no tail without effects");
    }

    // ---- level
    rig.reset(); setParam (p, 0, id::level, -6.0f);
    checkNear (peak (rig.play (0.6), sr, 0.2, 0.5), 0.5 * 0.5012, 0.005, "Level -6 dB");
    rig.reset(); setParam (p, 0, id::level, 6.0f);
    checkNear (peak (rig.play (0.6), sr, 0.2, 0.5), 0.9976, 0.03, "Level +6 dB (just under the limiter)");
    rig.reset(); setParam (p, 0, id::level, -60.0f);
    check (peak (rig.play (0.6), sr, 0.0, 0.6) < 1.0e-6f, "Level at minimum is silent");

    // ---- pan (constant power, centre = unity)
    rig.reset(); setParam (p, 0, id::pan, -100.0f);
    {
        const auto out = rig.play (0.6);
        checkNear (peak (out, sr, 0.2, 0.5, 0), 0.5 * std::sqrt (2.0), 0.01, "Pan left: left channel +3 dB");
        check (peak (out, sr, 0.2, 0.5, 1) < 1.0e-4f, "Pan left: right channel silent");
    }
    rig.reset(); setParam (p, 0, id::pan, 50.0f);
    {
        const auto out = rig.play (0.6);
        const float l = peak (out, sr, 0.2, 0.5, 0), r = peak (out, sr, 0.2, 0.5, 1);
        check (r > l, "Pan right: right louder");
        checkNear (l * l + r * r, 0.5, 0.01, "Pan keeps the total power constant");
    }

    // ---- EQ (measured at 440 Hz with the mid band tuned there; -12 dB level keeps it away from the limiter)
    rig.reset(); setParam (p, 0, id::level, -12.0f);
    const float reference = peak (rig.play (0.6), sr, 0.2, 0.5);
    rig.reset(); setParam (p, 0, id::level, -12.0f); setParam (p, 0, id::eqMidFreq, 440.0f); setParam (p, 0, id::eqMid, 12.0f);
    checkNear (peak (rig.play (0.6), sr, 0.3, 0.5) / reference, 3.98, 0.3, "EQ mid +12 dB");
    rig.reset(); setParam (p, 0, id::level, -12.0f); setParam (p, 0, id::eqMidFreq, 440.0f); setParam (p, 0, id::eqMid, -12.0f);
    checkNear (peak (rig.play (0.6), sr, 0.3, 0.5) / reference, 0.251, 0.03, "EQ mid -12 dB");
    rig.reset(); setParam (p, 0, id::level, -12.0f); setParam (p, 0, id::eqLow, -12.0f);
    { const float ratio = peak (rig.play (0.6), sr, 0.3, 0.5) / reference;
      check (ratio < 0.98f && ratio > 0.5f, "EQ low shelf cut reaches 440 Hz a little (x" + juce::String (ratio, 3) + ")"); }
    rig.reset(); setParam (p, 0, id::level, -12.0f); setParam (p, 0, id::eqHigh, 12.0f);
    checkNear (peak (rig.play (0.6), sr, 0.3, 0.5) / reference, 1.0, 0.12, "EQ high shelf leaves 440 Hz almost alone");

    // ---- drive: more harmonics, similar level
    rig.reset(); setParam (p, 0, id::drive, 100.0f);
    {
        const auto out = rig.play (0.6);
        checkNear (peak (out, sr, 0.2, 0.5), 0.5, 0.1, "Drive keeps the peak level");
        check (rms (out, sr, 0.2, 0.5) > 0.5f / std::sqrt (2.0f) * 1.15f, "Drive squares the wave (higher RMS)");
        checkNear (frequency (out, sr, 0.2, 0.5), 440.0, 3.0, "Drive keeps the pitch");
        check (allFinite (out), "Drive output is finite");
    }
    rig.reset(); setParam (p, 0, id::drive, 3.0f);
    checkNear (peak (rig.play (0.6), sr, 0.2, 0.5), 0.5, 0.02, "tiny Drive is almost transparent (no jump at 0)");
}

//==============================================================================
static void testSendEffects (Rig& rig)
{
    section ("reverb / delay / limiter");
    auto& p = rig.p;

    // ---- reverb: tail after the sample has ended
    rig.reset(); setParam (p, 0, id::sendRev, 100.0f);
    {
        const auto out = rig.play (3.0);
        check (rms (out, sr, 1.1, 1.6) > 0.003f, "Reverb: tail after the sample (rms " + juce::String (rms (out, sr, 1.1, 1.6), 4) + ")");
        check (rms (out, sr, 1.1, 1.4) > rms (out, sr, 2.4, 2.9) * 1.5f, "Reverb: tail decays");
        check (allFinite (out) && out.getMagnitude (0, out.getNumSamples()) <= 1.0f, "Reverb: bounded output");
        check (std::abs (rms (out, sr, 1.1, 1.6, 0) - rms (out, sr, 1.1, 1.6, 1)) > 1.0e-5f, "Reverb: left and right differ (stereo)");
    }
    rig.reset(); setParam (p, 0, id::sendRev, 100.0f); setParam (p, id::rvMix, 0.0f);
    check (rms (rig.play (2.0), sr, 1.1, 1.6) < 1.0e-5f, "Reverb Mix 0 removes the return");
    rig.reset(); setParam (p, 0, id::sendRev, 100.0f); setParam (p, id::rvDecay, 0.3f);
    const float shortTail = rms (rig.play (2.6), sr, 1.5, 2.5);
    rig.reset(); setParam (p, 0, id::sendRev, 100.0f); setParam (p, id::rvDecay, 8.0f);
    check (rms (rig.play (2.6), sr, 1.5, 2.5) > shortTail * 5.0f + 1.0e-4f, "Reverb Decay lengthens the tail");
    rig.reset(); setParam (p, 0, id::sendRev, 100.0f); setParam (p, id::rvPreDelay, 200.0f); setParam (p, id::rvSize, 0.0f);
    {
        // burst pad: nothing wet for the first ~200 ms
        const auto wet = render (p, sr, 1.0, { { 0, noteOn (37) } });
        rig.reset();
        const auto dryBurst = render (p, sr, 1.0, { { 0, noteOn (37) } });
        float early = 0.0f;
        for (int i = 0; i < (int) (0.19 * sr); ++i)
            early = juce::jmax (early, std::abs (wet.getSample (0, i) - dryBurst.getSample (0, i)));
        check (early < 1.0e-4f, "Reverb Pre-Delay holds the reverb back");
    }

    // ---- delay: echo of the 0.1 s burst, synced to the tempo
    auto delayTest = [&] (float division, TestPlayHead* playHead, double expectedSeconds, const juce::String& label)
    {
        rig.reset();
        setParam (p, 1, id::sendDly, 100.0f);
        setParam (p, id::dlMix, 100.0f);
        setParam (p, id::dlFeedback, 0.0f);
        setParam (p, id::dlDivision, division);
        const auto out = render (p, sr, 1.5, { { 0, noteOn (37) } }, 512, playHead);
        check (peak (out, sr, expectedSeconds + 0.01, expectedSeconds + 0.09) > 0.25f, label + ": echo at " + juce::String (expectedSeconds, 3) + " s");
        check (peak (out, sr, 0.12, expectedSeconds - 0.02) < 0.01f, label + ": silence before the echo");
        check (peak (out, sr, expectedSeconds + 0.13, expectedSeconds + 0.3) < 0.01f, label + ": one repeat only without feedback");
    };
    delayTest ((float) TempoSync::divQuarter, nullptr, 0.5, "Delay 1/4, manual tempo 120");
    delayTest ((float) TempoSync::divEighthDotted, nullptr, 0.375, "Delay 1/8., manual tempo 120");
    {
        TestPlayHead host; host.bpm = 150.0;
        delayTest ((float) TempoSync::divQuarter, &host, 0.4, "Delay 1/4, host tempo 150");
    }
    {
        rig.reset(); setParam (p, id::tempo, 100.0f);
        setParam (p, 1, id::sendDly, 100.0f); setParam (p, id::dlMix, 100.0f); setParam (p, id::dlFeedback, 0.0f);
        setParam (p, id::dlDivision, (float) TempoSync::divQuarter);
        const auto out = render (p, sr, 1.5, { { 0, noteOn (37) } });
        check (peak (out, sr, 0.61, 0.69) > 0.25f && peak (out, sr, 0.12, 0.58) < 0.01f, "Delay follows the manual tempo (100 BPM -> 600 ms)");
        check (! p.isUsingHostTempo() && std::abs (p.getCurrentBpm() - 100.0) < 1e-6, "fallback tempo is reported");
    }
    {
        rig.reset();
        setParam (p, 1, id::sendDly, 100.0f); setParam (p, id::dlMix, 100.0f); setParam (p, id::dlFeedback, 0.0f);
        setParam (p, id::dlSync, 0.0f); setParam (p, id::dlTime, 250.0f);
        const auto out = render (p, sr, 1.0, { { 0, noteOn (37) } });
        check (peak (out, sr, 0.26, 0.34) > 0.25f && peak (out, sr, 0.12, 0.23) < 0.01f, "Delay free time 250 ms");
    }
    {
        rig.reset();
        setParam (p, 1, id::sendDly, 100.0f); setParam (p, id::dlMix, 100.0f); setParam (p, id::dlFeedback, 60.0f);
        setParam (p, id::dlDivision, (float) TempoSync::divQuarter);
        const auto out = render (p, sr, 2.4, { { 0, noteOn (37) } });
        const float first = peak (out, sr, 0.51, 0.59), second = peak (out, sr, 1.01, 1.09), third = peak (out, sr, 1.51, 1.59);
        check (second > 0.05f && second < first && third < second && third > 0.01f, "Delay Feedback repeats and decays");

        setParam (p, id::dlPingPong, 1.0f);
        p.releaseResources();
        const auto pp = render (p, sr, 1.3, { { 0, noteOn (37) } });
        check (peak (pp, sr, 0.51, 0.59, 0) > 0.2f && peak (pp, sr, 0.51, 0.59, 1) < 0.01f, "Ping-pong: first repeat on the left");
        check (peak (pp, sr, 1.01, 1.09, 1) > 0.05f && peak (pp, sr, 1.01, 1.09, 0) < 0.01f, "Ping-pong: second repeat on the right");
    }

    // ---- limiter: 32 stacked voices never exceed full scale
    rig.reset();
    {
        std::vector<Event> events;
        for (int i = 0; i < 32; ++i) events.push_back ({ 0, noteOn (36) });   // all in phase
        const auto out = render (p, sr, 1.2, events);
        check (allFinite (out), "Limiter: finite output");
        check (out.getMagnitude (0, out.getNumSamples()) <= 1.0f && out.getMagnitude (0, out.getNumSamples()) > 0.8f,
               "Limiter: loud input is held at full scale (peak " + juce::String (out.getMagnitude (0, out.getNumSamples()), 3) + ")");
    }
}

//==============================================================================
static void testGenre (Rig& rig)
{
    section ("genre knobs");
    auto& p = rig.p;

    rig.reset();
    const auto dry = rig.play (1.6);

    static const char* const knobs[] = { id::bloom, id::grain, id::drift, id::warmth, id::dew,
                                         id::pump, id::wobble, id::gate, id::width, id::crush, id::dive, id::formant };
    for (auto* knob : knobs)
    {
        rig.reset();
        setParam (p, 0, knob, 100.0f);
        TestPlayHead host;
        const auto out = rig.play (1.6, 36, &host);
        const juce::String name (knob);
        check (allFinite (out), name + ": finite");
        check (out.getMagnitude (0, out.getNumSamples()) <= 1.0f, name + ": within full scale");
        check (rms (out, sr, 0.0, 1.0) > 0.02f, name + ": still audible");
        check (maxDifference (out, dry) > 0.02f, name + ": changes the sound");

        // knob back to 0 -> exactly the dry signal again (true bypass)
        rig.reset();
        { const auto again = rig.play (1.6); int first = -1; for (int i = 0; i < again.getNumSamples() && first < 0; ++i) if (std::abs (again.getSample (0, i) - dry.getSample (0, i)) > 1.0e-6f) first = i;
          check (maxDifference (again, dry) < 1.0e-6f, name + ": 0 % is a true bypass (max diff " + juce::String (maxDifference (again, dry), 6) + " first at " + juce::String (first) + ")"); }
    }

    // ---- Pump: ducks on every beat (1/4 at 120 BPM = every 0.5 s)
    rig.reset(); setParam (p, 0, id::pump, 100.0f);
    {
        TestPlayHead host;
        const auto out = rig.play (1.0, 36, &host);
        const float open = rms (out, sr, 0.36, 0.46);
        check (rms (out, sr, 0.005, 0.04) < open * 0.3f, "Pump: ducked right after beat 1");
        checkNear (open, 0.3535, 0.03, "Pump: back to full level before the next beat");
        check (rms (out, sr, 0.505, 0.54) < open * 0.3f, "Pump: ducked again on beat 2");
        check (maxStep (out, sr, 0.0, 1.0) < 0.06f, "Pump: no clicks");
    }
    rig.reset(); setParam (p, 0, id::pump, 100.0f); setParam (p, id::pumpRate, (float) TempoSync::divEighth);
    {
        TestPlayHead host;
        const auto out = rig.play (1.0, 36, &host);
        check (rms (out, sr, 0.255, 0.28) < rms (out, sr, 0.19, 0.24) * 0.4f, "Pump Rate 1/8: also ducks on the off-beat");
    }

    // ---- Gate: 16 steps, here on/off alternating at 1/16 (125 ms per step)
    rig.reset(); setParam (p, 0, id::gate, 100.0f);
    for (int s = 0; s < 16; ++s) setParam (p, id::gateStep (s), (s % 2 == 0) ? 1.0f : 0.0f);
    {
        TestPlayHead host;
        const auto out = rig.play (1.0, 36, &host);
        checkNear (rms (out, sr, 0.03, 0.11), 0.3535, 0.03, "Gate: open step plays");
        check (rms (out, sr, 0.145, 0.24) < 0.01f, "Gate: closed step is silent");
        checkNear (rms (out, sr, 0.27, 0.36), 0.3535, 0.03, "Gate: next open step plays");
        check (maxStep (out, sr, 0.0, 1.0) < 0.08f, "Gate: edges are smoothed");
    }
    rig.reset(); setParam (p, 0, id::gate, 50.0f);
    for (int s = 0; s < 16; ++s) setParam (p, id::gateStep (s), (s % 2 == 0) ? 1.0f : 0.0f);
    {
        TestPlayHead host;
        checkNear (rms (rig.play (1.0, 36, &host), sr, 0.16, 0.24), 0.3535 * 0.5, 0.02, "Gate 50 %: closed steps at half level");
    }

    // ---- Wobble: the filter closes in the middle of each cycle (1/8 at 120 = 250 ms)
    rig.reset(); setParam (p, 0, id::wobble, 100.0f);
    {
        TestPlayHead host;
        const auto out = rig.play (1.0, 36, &host);
        check (rms (out, sr, 0.11, 0.14) < rms (out, sr, 0.24, 0.26) * 0.6f, "Wobble: darker / quieter mid-cycle than on the beat");
    }

    // ---- Dive: starts an octave low, returns to pitch, tempo unchanged
    rig.reset(); setParam (p, 0, id::dive, 100.0f); setParam (p, id::diveTime, 500.0f);
    {
        const auto out = rig.play (1.5);
        const double early = frequency (out, sr, 0.0, 0.06);
        check (early > 200.0 && early < 300.0, "Dive: starts about an octave down (" + juce::String (early, 1) + " Hz)");
        checkNear (frequency (out, sr, 0.7, 0.95), 440.0, 8.0, "Dive: back at the normal pitch");
        checkNear (soundingLength (out, sr), 1.0, 0.07, "Dive: length (tempo) unchanged");
    }
    rig.reset(); setParam (p, 0, id::dive, 100.0f); setParam (p, id::diveRange, 12.0f); setParam (p, id::diveTime, 500.0f);
    {
        const double early = frequency (rig.play (1.5), sr, 0.0, 0.06);
        check (early > 700.0 && early < 900.0, "Dive Range +12: starts an octave up (" + juce::String (early, 1) + " Hz)");
    }

    // ---- Drift: small pitch wander, tempo unchanged
    rig.reset(); setParam (p, 0, id::drift, 100.0f);
    {
        const auto out = rig.play (1.5);
        const double f = frequency (out, sr, 0.1, 0.9);
        check (std::abs (f - 440.0) < 440.0 * 0.035, "Drift: pitch stays close (" + juce::String (f, 1) + " Hz)");
        checkNear (soundingLength (out, sr), 1.0, 0.07, "Drift: length (tempo) unchanged");
    }

    // ---- Formant: same pitch and length
    rig.reset(); setParam (p, 0, id::formant, 100.0f);
    {
        const auto out = rig.play (1.5);
        checkNear (frequency (out, sr, 0.2, 0.8), 440.0, 10.0, "Formant: pitch unchanged");
        checkNear (soundingLength (out, sr), 1.0, 0.07, "Formant: length unchanged");
    }

    // ---- Width: a mono-compatible source becomes stereo
    rig.reset(); setParam (p, 0, id::width, 100.0f);
    {
        const auto out = rig.play (0.8);
        float difference = 0.0f;
        for (int i = (int) (0.2 * sr); i < (int) (0.7 * sr); ++i)
            difference = juce::jmax (difference, std::abs (out.getSample (0, i) - out.getSample (1, i)));
        check (difference > 0.05f, "Width: left and right differ");
    }

    // ---- Bloom: long shimmer tail
    rig.reset(); setParam (p, 0, id::bloom, 100.0f);
    {
        const auto out = rig.play (4.0);
        check (rms (out, sr, 1.3, 2.5) > 0.002f, "Bloom: tail after the sample (rms " + juce::String (rms (out, sr, 1.3, 2.5), 4) + ")");
        check (allFinite (out) && out.getMagnitude (0, out.getNumSamples()) <= 1.0f, "Bloom: bounded");
    }

    // ---- everything at once on a busy pad stays sane
    rig.reset();
    for (auto* knob : knobs) setParam (p, 0, knob, 100.0f);
    setParam (p, 0, id::drive, 100.0f); setParam (p, 0, id::sendRev, 100.0f); setParam (p, 0, id::sendDly, 100.0f);
    setParam (p, id::dlFeedback, 95.0f); setParam (p, id::rvDecay, 12.0f); setParam (p, 0, id::level, 6.0f);
    {
        TestPlayHead host;
        std::vector<Event> events;
        for (int i = 0; i < 12; ++i) events.push_back ({ i * 9, noteOn (36) });
        const auto out = render (p, sr, 6.0, events, 512, &host);
        check (allFinite (out), "all knobs at maximum: finite");
        check (out.getMagnitude (0, out.getNumSamples()) <= 1.0f && out.getMagnitude (1, 0, out.getNumSamples()) <= 1.0f,
               "all knobs at maximum: within full scale");
    }
}

//==============================================================================
static void testModulation (Rig& rig)
{
    section ("modulation");
    auto& p = rig.p;
    auto& matrix = p.getModMatrix();

    // ---- matrix arithmetic
    rig.reset();
    {
        const int drive = p.getPadTarget (0, id::fxDrive);
        const int level = p.getPadTarget (0, id::fxLevel);
        setParam (p, 0, id::drive, 40.0f);

        const float none[4] = { 0, 0, 0, 0 };
        matrix.update (none, 64);
        checkNear (matrix.get (drive), 40.0, 1e-4, "no assignment: plain parameter value");

        const int slot = matrix.assign (0, drive, 0.5f);
        check (slot >= 0 && matrix.getSlots().size() == 1, "assign creates a slot");
        check (matrix.assign (0, drive, 0.25f) == slot && matrix.getSlots().size() == 1, "same source + target updates the depth");
        checkNear (matrix.getSlots()[0].depth, 0.25, 1e-6, "depth updated");

        const float up[4] = { 1.0f, 0, 0, 0 }, down[4] = { -1.0f, 0, 0, 0 };
        matrix.update (up, 64);
        checkNear (matrix.get (drive), 65.0, 1e-3, "depth +25 % of the range at mod = +1");
        matrix.update (down, 64);
        checkNear (matrix.get (drive), 15.0, 1e-3, "depth +25 % at mod = -1 goes down");

        matrix.assign (1, drive, 1.0f);
        const float both[4] = { 1.0f, 1.0f, 0, 0 };
        matrix.update (both, 64);
        checkNear (matrix.get (drive), 100.0, 1e-3, "sum of sources is clamped to the knob's range");
        checkNear (matrix.getTotalDepth (drive), 1.25, 1e-6, "total depth");
        check (matrix.getTarget (drive).numAssignments.load() == 2, "assignment count");

        matrix.publishForUi();
        checkNear (matrix.getTarget (drive).uiOffset.load(), 1.25, 1e-6, "offset published for the knob marker");

        matrix.removeAllFor (drive);
        matrix.update (both, 64);
        checkNear (matrix.get (drive), 40.0, 1e-4, "removed: back to the parameter value");

        // skewed parameter: modulation works in knob (normalised) units
        matrix.assign (2, level, -1.0f);
        const float m3[4] = { 0, 0, 1.0f, 0 };
        matrix.update (m3, 64);
        checkNear (matrix.get (level), -60.0, 1e-3, "full negative depth reaches the bottom of the range");
        matrix.clear();
        check (matrix.getSlots().empty(), "clear removes everything");

        for (int i = 0; i < ModMatrix::maxSlots; ++i) matrix.assign (i % 4, i / 4, 0.1f);
        check ((int) matrix.getSlots().size() == ModMatrix::maxSlots, "all slots can be used");
        check (matrix.assign (0, 200, 0.1f) == -1, "a full table refuses more");
        matrix.clear();
    }

    // ---- square LFO on Level, locked to the host position (1/4 at 120 BPM)
    rig.reset();
    setParam (p, id::mod (0, id::modType), (float) TempoSync::square);
    setParam (p, id::mod (0, id::modRate), (float) TempoSync::divQuarter);
    setParam (p, id::mod (0, id::modMode), (float) TempoSync::hostPosition);
    matrix.assign (0, p.getPadTarget (0, id::fxLevel), -1.0f);
    {
        TestPlayHead host;
        const auto out = rig.play (1.0, 36, &host);
        check (rms (out, sr, 0.03, 0.22) < 0.005f, "LFO -> Level: silent while the square is high");
        check (rms (out, sr, 0.28, 0.47) > 0.4f, "LFO -> Level: loud while it is low");
        check (rms (out, sr, 0.53, 0.72) < 0.005f, "LFO -> Level: repeats every beat");
        check (maxStep (out, sr, 0.0, 1.0) < 0.12f, "LFO -> Level: smoothed, no hard clicks");
        check (std::abs (getParam (p, 0, id::level)) < 1e-6f, "modulation does not move the parameter itself");
        check (std::abs (p.getModMatrix().getTarget (p.getPadTarget (0, id::fxLevel)).uiOffset.load()) > 0.1f, "live offset visible to the UI");
    }
    {
        // host tempo change: at 60 BPM the same LFO takes 1 s per cycle
        TestPlayHead host; host.bpm = 60.0;
        p.releaseResources();
        const auto out = rig.play (1.0, 36, &host);
        check (rms (out, sr, 0.05, 0.45) < 0.005f && rms (out, sr, 0.55, 0.95) > 0.4f, "LFO follows the host tempo");
    }
    {
        // starting in the middle of a beat: the phase follows the song position
        TestPlayHead host; host.ppq = 8.5;
        p.releaseResources();
        const auto out = rig.play (0.4, 36, &host);
        check (rms (out, sr, 0.03, 0.22) > 0.4f, "LFO phase follows the host position");
    }

    // ---- retrigger: restarts at the note, wherever the song is
    setParam (p, id::mod (0, id::modMode), (float) TempoSync::retrigger);
    {
        TestPlayHead host; host.ppq = 8.5;
        p.releaseResources();
        const auto out = rig.play (0.4, 36, &host);
        check (rms (out, sr, 0.03, 0.22) < 0.005f, "Retrigger: the cycle restarts at note-on");
    }

    // ---- free rate in Hz (4 Hz -> 0.25 s per cycle)
    rig.reset();
    setParam (p, id::mod (1, id::modType), (float) TempoSync::square);
    setParam (p, id::mod (1, id::modSync), 0.0f);
    setParam (p, id::mod (1, id::modHz), 4.0f);
    setParam (p, id::mod (1, id::modMode), (float) TempoSync::retrigger);
    matrix.assign (1, p.getPadTarget (0, id::fxLevel), -1.0f);
    {
        const auto out = rig.play (0.6);
        check (rms (out, sr, 0.02, 0.11) < 0.005f && rms (out, sr, 0.14, 0.24) > 0.4f && rms (out, sr, 0.31, 0.365) < 0.005f,
               "free-running rate in Hz");
    }

    // ---- step sequencer on Pan: step 1 hard left, step 2 hard right (1/16 steps = 125 ms)
    rig.reset();
    setParam (p, id::mod (3, id::modType), (float) TempoSync::stepSeq);
    setParam (p, id::mod (3, id::modRate), (float) TempoSync::divSixteenth);
    for (int s = 0; s < 16; ++s) setParam (p, id::modStep (3, s), (s % 2 == 0) ? -1.0f : 1.0f);
    matrix.assign (3, p.getPadTarget (0, id::fxPan), 0.5f);
    {
        TestPlayHead host;
        const auto out = rig.play (0.6, 36, &host);
        check (rms (out, sr, 0.04, 0.11, 0) > 0.4f && rms (out, sr, 0.04, 0.11, 1) < 0.02f, "Step 1: panned left");
        check (rms (out, sr, 0.165, 0.24, 1) > 0.4f && rms (out, sr, 0.165, 0.24, 0) < 0.02f, "Step 2: panned right");
    }

    // ---- modulating a global FX knob (Reverb Mix) and a genre knob
    rig.reset();
    setParam (p, 0, id::sendRev, 100.0f);
    setParam (p, id::mod (0, id::modType), (float) TempoSync::square);
    setParam (p, id::mod (0, id::modRate), 0.0f);        // 4 bars: stays "high" for the whole test
    matrix.assign (0, matrix.findTarget (id::rvMix), -1.0f);
    {
        TestPlayHead host;
        check (rms (rig.play (2.0, 36, &host), sr, 1.1, 1.6) < 1.0e-4f, "modulated Reverb Mix closes the return");
    }
    rig.reset();
    setParam (p, id::mod (0, id::modType), (float) TempoSync::square);
    setParam (p, id::mod (0, id::modRate), 0.0f);
    matrix.assign (0, p.getPadTarget (0, id::fxCrush), 1.0f);
    {
        TestPlayHead host;
        const auto crushed = rig.play (0.6, 36, &host);
        rig.reset();
        check (maxDifference (crushed, rig.play (0.6)) > 0.05f, "a genre knob at 0 can be opened by a modulator");
    }
}

//==============================================================================
static void testRandomizer (Rig& rig)
{
    section ("randomiser");
    auto& p = rig.p;
    auto& matrix = p.getModMatrix();
    auto& randomizer = p.getRandomizer();
    randomizer.setSeed (20240607);

    auto snapshot = [&]
    {
        std::vector<float> values;
        for (int i = 0; i < matrix.getNumTargets(); ++i)
            values.push_back (matrix.getTarget (i).param->getValue());
        return values;
    };
    auto countChanged = [&] (const std::vector<float>& before, const std::vector<float>& after, int pad)
    {
        int n = 0;
        for (int i = 0; i < matrix.getNumTargets(); ++i)
            if (matrix.getTarget (i).pad == pad && std::abs (before[(size_t) i] - after[(size_t) i]) > 1e-6f)
                ++n;
        return n;
    };

    rig.reset();
    randomizer.clearUndo();
    check (! randomizer.canUndo(), "nothing to undo at first");
    check (matrix.getTarget (p.getPadTarget (0, id::fxLevel)).locked.load(), "Level is locked by default");
    check (! matrix.getTarget (p.getPadTarget (0, id::fxDrive)).locked.load(), "other knobs start unlocked");

    // ---- selected pad only
    matrix.getTarget (p.getPadTarget (2, id::fxWarmth)).locked.store (true);
    const auto before = snapshot();
    const int changed = randomizer.randomize (Randomizer::padScope, 0.5f, 2);
    const auto after = snapshot();
    check (changed >= 4, "randomise moves the pad's knobs (" + juce::String (changed) + " changed)");
    check (countChanged (before, after, 2) == changed, "only the selected pad changes");
    check (countChanged (before, after, 0) == 0 && countChanged (before, after, -1) == 0, "other pads and the global FX stay");
    check (std::abs (getParam (p, 2, id::level)) < 1e-6f, "locked Level did not move");
    check (std::abs (getParam (p, 2, id::warmth)) < 1e-6f, "a knob locked by the user did not move");
    {
        bool withinAmount = true;
        for (int i = 0; i < matrix.getNumTargets(); ++i)
            withinAmount = withinAmount && std::abs (after[(size_t) i] - before[(size_t) i]) <= 0.51f;
        check (withinAmount, "changes stay within the Amount");
    }
    check (std::abs (getParam (p, 2, id::srcBpm) - 120.0f) < 1e-6f && std::abs (getParam (p, 2, id::semitones)) < 1e-6f,
           "BPM and pitch conversion are never randomised");

    // ---- undo
    check (randomizer.canUndo() && randomizer.undo(), "undo");
    {
        const auto restored = snapshot();
        float worst = 0.0f;
        for (size_t i = 0; i < restored.size(); ++i) worst = juce::jmax (worst, std::abs (restored[i] - before[i]));
        check (worst < 1e-6f, "undo restores every value");
    }
    check (! randomizer.canUndo(), "undo stack is empty again");

    // two steps of undo
    randomizer.randomize (Randomizer::padScope, 0.3f, 2);
    const auto afterFirst = snapshot();
    randomizer.randomize (Randomizer::padScope, 0.3f, 2);
    check (randomizer.undo(), "undo second randomise");
    {
        const auto now = snapshot();
        float worst = 0.0f;
        for (size_t i = 0; i < now.size(); ++i) worst = juce::jmax (worst, std::abs (now[i] - afterFirst[i]));
        check (worst < 1e-6f, "first undo returns to the state in between");
    }
    check (randomizer.undo() && ! randomizer.canUndo(), "undo first randomise");

    // ---- global FX scope, and both
    {
        const auto b = snapshot();
        randomizer.randomize (Randomizer::fxScope, 0.4f, 2);
        const auto a = snapshot();
        check (countChanged (b, a, -1) >= 8 && countChanged (b, a, 2) == 0, "FX scope: only the global effects change");
        randomizer.undo();
        randomizer.randomize (Randomizer::bothScope, 0.4f, 2);
        const auto c = snapshot();
        check (countChanged (b, c, -1) >= 8 && countChanged (b, c, 2) >= 4 && countChanged (b, c, 5) == 0, "Pad + FX scope");
        randomizer.undo();
    }

    // ---- amount 0 changes nothing
    {
        const auto b = snapshot();
        check (randomizer.randomize (Randomizer::bothScope, 0.0f, 2) == 0, "Amount 0 changes nothing");
        randomizer.undo();
        juce::ignoreUnused (b);
    }

    // ---- result is always playable
    rig.reset();
    for (int round = 0; round < 6; ++round)
    {
        randomizer.randomize (Randomizer::bothScope, 1.0f, 0);
        TestPlayHead host;
        const auto out = rig.play (0.8, 36, &host);
        if (! allFinite (out) || out.getMagnitude (0, out.getNumSamples()) > 1.0f)
        {
            check (false, "randomised settings produced bad audio in round " + juce::String (round));
            break;
        }
    }
    check (true, "fully randomised settings stay finite and within full scale");

    // ---- auto-randomise: tempo-synced temporary offsets, parameters untouched
    rig.reset();
    randomizer.clearUndo();
    setParam (p, id::rndAuto, 1.0f);
    setParam (p, id::rndInterval, 0.0f);     // every beat
    setParam (p, id::rndAmount, 50.0f);
    setParam (p, id::rndScope, (float) Randomizer::padScope);
    {
        const auto b = snapshot();
        TestPlayHead host;
        rig.play (0.45, 36, &host);
        auto offsets = [&]
        {
            std::vector<float> o;
            for (int k = 0; k < SimpleSamplerProcessor::numPadFx; ++k)
                o.push_back (matrix.getTarget (p.getPadTarget (0, k)).uiOffset.load());
            return o;
        };
        const auto first = offsets();
        float total = 0.0f;
        for (float v : first) total += std::abs (v);
        check (total > 0.5f, "auto-randomise applies offsets");
        check (std::abs (first[(size_t) id::fxLevel]) < 1e-6f, "auto-randomise respects locks");
        check (std::abs (matrix.getTarget (p.getPadTarget (1, id::fxDrive)).uiOffset.load()) < 1e-6f, "auto-randomise only touches the selected pad");

        const auto a = snapshot();
        float moved = 0.0f;
        for (size_t i = 0; i < a.size(); ++i) moved = juce::jmax (moved, std::abs (a[i] - b[i]));
        check (moved < 1e-6f, "auto-randomise does not change the stored parameter values");

        // still inside the same beat: nothing new
        rig.play (0.03, 37, &host);
        float drift = 0.0f;
        { const auto same = offsets(); for (size_t i = 0; i < same.size(); ++i) drift = juce::jmax (drift, std::abs (same[i] - first[i])); }
        check (drift < 0.02f, "offsets hold until the next beat");

        // crossing the beat: new values
        rig.play (0.3, 37, &host);
        float change = 0.0f;
        { const auto next = offsets(); for (size_t i = 0; i < next.size(); ++i) change = juce::jmax (change, std::abs (next[i] - first[i])); }
        check (change > 0.05f, "new offsets on the next beat");

        // the offsets glide (no jump): render across a beat with only Level-neutral knobs -> no click
        setParam (p, id::rndAuto, 0.0f);
        rig.play (0.6, 37, &host);
        float left = 0.0f;
        for (float v : offsets()) left += std::abs (v);
        check (left < 1e-3f, "switching auto-randomise off returns to the dialled-in sound");
    }
}

//==============================================================================
static void testStateV2 (Rig& rig)
{
    section ("state version 2 and older formats");

    // ---- round trip of everything new
    juce::MemoryBlock saved;
    {
        auto& a = rig.p;
        rig.reset();
        setParam (a, 0, id::level, -7.5f);   setParam (a, 0, id::pan, -30.0f);
        setParam (a, 0, id::eqMidFreq, 2500.0f); setParam (a, 0, id::eqHigh, 4.0f);
        setParam (a, 3, id::bloom, 55.0f);   setParam (a, 3, id::dive, 80.0f);
        setParam (a, id::rvDecay, 6.5f);     setParam (a, id::dlDivision, (float) TempoSync::divSixteenth);
        setParam (a, id::dlSync, 0.0f);      setParam (a, id::dlPingPong, 1.0f);
        setParam (a, id::gateStep (3), 1.0f); setParam (a, id::gateStep (0), 0.0f);
        setParam (a, id::mod (1, id::modType), (float) TempoSync::stepSeq);
        setParam (a, id::modStep (1, 7), -0.25f);
        setParam (a, id::mod (2, id::modMode), (float) TempoSync::retrigger);
        setParam (a, id::tempo, 97.0f);      setParam (a, id::rndAmount, 12.0f);
        setParam (a, id::rndInterval, 4.0f);

        auto& m = a.getModMatrix();
        m.assign (0, a.getPadTarget (0, id::fxPan), 0.4f);
        m.assign (3, m.findTarget (id::dlFeedback), -0.65f);
        m.getTarget (a.getPadTarget (0, id::fxDrive)).locked.store (true);     // user lock
        m.getTarget (a.getPadTarget (5, id::fxLevel)).locked.store (false);    // user unlocked a default lock
        a.editorTab.store (2);

        const auto tree = a.createStateTree();
        check ((int) tree.getProperty ("version") == SimpleSamplerProcessor::currentStateVersion && SimpleSamplerProcessor::currentStateVersion == 2,
               "state is written as version 2");
        a.getStateInformation (saved);
    }
    {
        SimpleSamplerProcessor b;
        b.prepareToPlay (sr, 512);
        b.setStateInformation (saved.getData(), (int) saved.getSize());
        check (waitForLoads (b), "samples reload");

        checkNear (getParam (b, 0, id::level), -7.5, 1e-3, "restore Level");
        checkNear (getParam (b, 0, id::pan), -30.0, 1e-3, "restore Pan");
        checkNear (getParam (b, 0, id::eqMidFreq), 2500.0, 0.6, "restore EQ mid frequency");
        checkNear (getParam (b, 0, id::eqHigh), 4.0, 1e-3, "restore EQ high");
        checkNear (getParam (b, 3, id::bloom), 55.0, 1e-3, "restore Bloom");
        checkNear (getParam (b, 3, id::dive), 80.0, 1e-3, "restore Dive");
        checkNear (getParam (b, id::rvDecay), 6.5, 1e-2, "restore Reverb Decay");
        checkNear (getParam (b, id::dlDivision), TempoSync::divSixteenth, 1e-6, "restore Delay division");
        check (getParam (b, id::dlSync) < 0.5f && getParam (b, id::dlPingPong) > 0.5f, "restore Delay switches");
        check (getParam (b, id::gateStep (3)) > 0.5f && getParam (b, id::gateStep (0)) < 0.5f, "restore Gate pattern");
        checkNear (getParam (b, id::mod (1, id::modType)), TempoSync::stepSeq, 1e-6, "restore modulator type");
        checkNear (getParam (b, id::modStep (1, 7)), -0.25, 1e-3, "restore step value");
        checkNear (getParam (b, id::mod (2, id::modMode)), TempoSync::retrigger, 1e-6, "restore modulator mode");
        checkNear (getParam (b, id::tempo), 97.0, 1e-3, "restore manual tempo");
        checkNear (getParam (b, id::rndAmount), 12.0, 1e-3, "restore randomise amount");
        checkNear (getParam (b, id::rndInterval), 4.0, 1e-6, "restore randomise interval");
        check (b.editorTab.load() == 2, "restore editor tab");

        auto& m = b.getModMatrix();
        const auto slots = m.getSlots();
        check (slots.size() == 2, "restore two assignments");
        bool pan = false, feedback = false;
        for (const auto& s : slots)
        {
            pan = pan || (s.source == 0 && s.target == b.getPadTarget (0, id::fxPan) && std::abs (s.depth - 0.4f) < 1e-5f);
            feedback = feedback || (s.source == 3 && s.target == m.findTarget (id::dlFeedback) && std::abs (s.depth + 0.65f) < 1e-5f);
        }
        check (pan && feedback, "assignments keep source, target and depth");
        check (m.getTarget (b.getPadTarget (0, id::fxDrive)).locked.load(), "restore a user lock");
        check (! m.getTarget (b.getPadTarget (5, id::fxLevel)).locked.load(), "restore an unlocked default lock");
        check (m.getTarget (b.getPadTarget (6, id::fxLevel)).locked.load() && ! m.getTarget (b.getPadTarget (6, id::fxDrive)).locked.load(),
               "untouched locks keep their defaults");
        check (b.getPad (0).getSample() != nullptr, "sample restored");

        // loading a second state replaces (does not add to) the assignments
        b.setStateInformation (saved.getData(), (int) saved.getSize());
        check (b.getModMatrix().getSlots().size() == 2, "re-loading does not duplicate assignments");
    }

    // ---- a version 1 file (first release): only sampler parameters, no Modulation child
    const juce::String legacyXml =
        "<SimpleSamplerState version=\"1\" presetName=\"Old Kit\" selectedPad=\"2\" editorWidth=\"1000\" editorHeight=\"620\">"
          "<Parameters>"
            "<PARAM id=\"p01_note\" value=\"40.0\"/>"
            "<PARAM id=\"p01_srcBpm\" value=\"100.0\"/>"
            "<PARAM id=\"p01_dstBpm\" value=\"140.0\"/>"
            "<PARAM id=\"p01_semitones\" value=\"-2.0\"/>"
            "<PARAM id=\"p01_loop\" value=\"1.0\"/>"
            "<PARAM id=\"p01_loopStart\" value=\"0.25\"/>"
            "<PARAM id=\"p01_loopEnd\" value=\"0.75\"/>"
            "<PARAM id=\"p01_attack\" value=\"30.0\"/>"
            "<PARAM id=\"p01_sustain\" value=\"0.5\"/>"
            "<PARAM id=\"masterGain\" value=\"-3.0\"/>"
          "</Parameters>"
          "<Pads><Pad index=\"0\" path=\"" + rig.sine.getFullPathName() + "\" fileName=\"" + rig.sine.getFileName() + "\"/></Pads>"
        "</SimpleSamplerState>";
    {
        SimpleSamplerProcessor c;
        c.prepareToPlay (sr, 512);

        // make the processor "dirty" first: everything a v1 file does not know must come back to its default
        setParam (c, 0, id::level, -20.0f);   setParam (c, 0, id::grain, 70.0f);
        setParam (c, 4, id::sendRev, 60.0f);  setParam (c, id::rvMix, 10.0f);
        setParam (c, id::dlFeedback, 90.0f);  setParam (c, id::mod (0, id::modType), (float) TempoSync::square);
        setParam (c, id::rndAuto, 1.0f);      setParam (c, id::gateStep (1), 1.0f);
        setParam (c, 0, id::decay, 999.0f);   // a v1 parameter that is missing from the file above
        c.getModMatrix().assign (1, c.getPadTarget (0, id::fxPan), 0.9f);
        c.getModMatrix().getTarget (c.getPadTarget (0, id::fxDrive)).locked.store (true);

        const auto xml = juce::parseXML (legacyXml);
        check (xml != nullptr, "legacy XML parses");
        check (xml != nullptr && c.restoreStateTree (juce::ValueTree::fromXml (*xml)), "version 1 state is accepted");
        check (waitForLoads (c), "legacy samples load");

        checkNear (getParam (c, 0, id::note), 40.0, 1e-6, "v1: note number");
        checkNear (getParam (c, 0, id::srcBpm), 100.0, 1e-3, "v1: start BPM");
        checkNear (getParam (c, 0, id::dstBpm), 140.0, 1e-3, "v1: result BPM");
        checkNear (getParam (c, 0, id::semitones), -2.0, 1e-6, "v1: pitch shift");
        check (getParam (c, 0, id::loop) > 0.5f, "v1: loop switch");
        checkNear (getParam (c, 0, id::loopStart), 0.25, 1e-4, "v1: loop start");
        checkNear (getParam (c, 0, id::attack), 30.0, 0.11, "v1: attack");
        checkNear (getParam (c, 0, id::sustain), 0.5, 1e-3, "v1: sustain");
        checkNear (getParam (c, id::master), -3.0, 1e-3, "v1: output volume");
        check (c.getPresetName() == "Old Kit" && c.getSelectedPad() == 2, "v1: name and selected pad");
        check (c.getPad (0).getSample() != nullptr && c.getPad (0).getFile() == rig.sine, "v1: sample");

        checkNear (getParam (c, 0, id::level), 0.0, 1e-6, "v1: new Level -> default");
        checkNear (getParam (c, 0, id::grain), 0.0, 1e-6, "v1: new Grain -> default");
        checkNear (getParam (c, 4, id::sendRev), 0.0, 1e-6, "v1: new Reverb Send -> default");
        checkNear (getParam (c, id::rvMix), 70.0, 1e-4, "v1: Reverb Mix -> default");
        checkNear (getParam (c, id::dlFeedback), 40.0, 1e-4, "v1: Delay Feedback -> default");
        checkNear (getParam (c, id::mod (0, id::modType)), TempoSync::sine, 1e-6, "v1: modulator type -> default");
        check (getParam (c, id::rndAuto) < 0.5f, "v1: auto-randomise off");
        check (getParam (c, id::gateStep (1)) < 0.5f, "v1: gate pattern -> default");
        checkNear (getParam (c, 0, id::decay), 100.0, 0.11, "v1: a missing old parameter -> default");
        check (c.getModMatrix().getSlots().empty(), "v1: no modulation assignments");
        check (! c.getModMatrix().getTarget (c.getPadTarget (0, id::fxDrive)).locked.load()
               && c.getModMatrix().getTarget (c.getPadTarget (0, id::fxLevel)).locked.load(), "v1: default locks");

        // and it plays exactly like the first release did: 100 -> 140 BPM, looped, sustain 50 %
        const auto out = render (c, sr, 1.2, { { 0, noteOn (40) } });
        check (rms (out, sr, 0.6, 1.1) > 0.05f && allFinite (out), "v1 state plays");

        // re-saving upgrades the file
        const auto resaved = c.createStateTree();
        check ((int) resaved.getProperty ("version") == 2 && resaved.getChildWithName (ModMatrix::treeType).isValid(), "re-saved as version 2");
    }

   #if SS_HAS_PRESETS
    // ---- the same through a preset file on disk
    {
        auto dir = tempDir().getChildFile ("legacyPresets");
        dir.deleteRecursively();
        dir.createDirectory();
        const auto file = dir.getChildFile ("Old Kit.sspreset");
        file.replaceWithText (legacyXml);

        SimpleSamplerProcessor d;
        d.prepareToPlay (sr, 512);
        setParam (d, 7, id::crush, 44.0f);
        PresetManager presets (d, dir);
        check (presets.getPresetNames().contains ("Old Kit"), "old preset is listed");
        check (presets.loadPreset ("Old Kit"), "old preset loads");
        check (waitForLoads (d), "old preset samples load");
        checkNear (getParam (d, 0, id::dstBpm), 140.0, 1e-3, "old preset: result BPM");
        checkNear (getParam (d, 7, id::crush), 0.0, 1e-6, "old preset: new knobs at default");

        setParam (d, 0, id::warmth, 33.0f);
        d.getModMatrix().assign (2, d.getPadTarget (0, id::fxWarmth), 0.2f);
        check (presets.savePreset ("New Kit"), "save in the new format");
        d.resetToInit();
        check (d.getModMatrix().getSlots().empty(), "reset clears modulation");
        check (presets.loadPreset ("New Kit"), "load the new preset");
        checkNear (getParam (d, 0, id::warmth), 33.0, 1e-3, "new preset: genre knob");
        check (d.getModMatrix().getSlots().size() == 1, "new preset: modulation");
    }
   #endif

    // ---- tolerant of things it does not know (e.g. a file from a later version)
    {
        SimpleSamplerProcessor e;
        e.prepareToPlay (sr, 512);
        auto tree = e.createStateTree();
        tree.setProperty ("version", 99, nullptr);
        auto modulation = tree.getChildWithName (ModMatrix::treeType);
        juce::ValueTree unknown ("Slot");
        unknown.setProperty ("source", 9, nullptr);
        unknown.setProperty ("param", "doesNotExist", nullptr);
        unknown.setProperty ("depth", 3.0, nullptr);
        modulation.appendChild (unknown, nullptr);
        juce::ValueTree oddSource ("Slot");
        oddSource.setProperty ("source", 9, nullptr);
        oddSource.setProperty ("param", id::rvMix, nullptr);
        modulation.appendChild (oddSource, nullptr);
        tree.getChildWithName ("Parameters").appendChild (juce::ValueTree ("PARAM").setProperty ("id", "futureParam", nullptr).setProperty ("value", 1.0, nullptr), nullptr);

        check (e.restoreStateTree (tree), "unknown entries do not prevent loading");
        check (e.getModMatrix().getSlots().empty(), "unknown assignments are skipped");
        check (! e.restoreStateTree (juce::ValueTree ("SomethingElse")), "a foreign tree is rejected");
    }
}

//==============================================================================
static void testPerformance()
{
    section ("performance (16 pads, every effect and modulator on)");

    // a 2 s chord-like loop so that every voice keeps running
    juce::AudioBuffer<float> buffer (2, (int) (2.0 * sr));
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const double t = i / sr;
        const float v = 0.2f * (float) (std::sin (2764.6 * t) + 0.6 * std::sin (4146.9 * t) + 0.4 * std::sin (6220.4 * t));
        buffer.setSample (0, i, v);
        buffer.setSample (1, i, v * 0.9f);
    }
    const auto file = writeBuffer ("v2_perf.wav", buffer, sr);

    SimpleSamplerProcessor p;
    p.prepareToPlay (sr, 512);

    static const char* const knobs[] = { id::bloom, id::grain, id::drift, id::warmth, id::dew, id::pump, id::wobble,
                                         id::gate, id::width, id::crush, id::dive, id::formant, id::drive, id::sendRev, id::sendDly };
    std::vector<Event> events;
    for (int pad = 0; pad < 16; ++pad)
    {
        p.loadSampleIntoPadBlocking (pad, file);
        setParam (p, pad, id::loop, 1.0f);
        for (auto* knob : knobs) setParam (p, pad, knob, 60.0f);
        setParam (p, pad, id::eqLow, 3.0f); setParam (p, pad, id::eqMid, -4.0f); setParam (p, pad, id::eqHigh, 5.0f);
        setParam (p, pad, id::level, -14.0f);
        setParam (p, pad, id::srcBpm, 120.0f); setParam (p, pad, id::dstBpm, 132.0f);   // every voice uses the stretch engine
        events.push_back ({ pad, noteOn (36 + pad) });

        for (int m = 0; m < 4 && pad < 15; ++m)
            p.getModMatrix().assign (m, p.getPadTarget (pad, (pad + m * 3) % SimpleSamplerProcessor::numPadFx), 0.3f);
    }
    setParam (p, id::rndAuto, 1.0f);
    setParam (p, id::rndScope, (float) Randomizer::bothScope);

    TestPlayHead host;
    const double seconds = 6.0;
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto out = render (p, sr, seconds, events, 512, &host);
    const double elapsed = (juce::Time::getMillisecondCounterHiRes() - start) * 0.001;
    const double load = elapsed / seconds * 100.0;

    std::cout << "  16 stretched voices + all effects: " << juce::String (load, 1) << " % of one core"
              << " (" << p.getActiveVoiceCount() << " voices)" << std::endl;

    check (p.getActiveVoiceCount() == 16, "16 voices running");
    check (allFinite (out) && out.getMagnitude (0, out.getNumSamples()) <= 1.0f, "output finite and within full scale");
    check (rms (out, sr, 2.0, 6.0) > 0.01f, "output audible");
   #if JUCE_DEBUG
    juce::ignoreUnused (load);
   #else
    check (load < 70.0, "runs comfortably faster than real time (" + juce::String (load, 1) + " %)");
   #endif

    // idle cost: nothing playing, nothing assigned
    SimpleSamplerProcessor idle;
    idle.prepareToPlay (sr, 512);
    const auto idleStart = juce::Time::getMillisecondCounterHiRes();
    render (idle, sr, 10.0, {});
    const double idleLoad = (juce::Time::getMillisecondCounterHiRes() - idleStart) * 0.001 / 10.0 * 100.0;
    std::cout << "  idle: " << juce::String (idleLoad, 2) << " % of one core" << std::endl;
   #if ! JUCE_DEBUG
    check (idleLoad < 3.0, "idle instance costs almost nothing");
   #endif
}

//==============================================================================
void runV2Tests()
{
    testTempoMaths();
    testParameters();

    Rig rig;
    testMixer (rig);
    testSendEffects (rig);
    testGenre (rig);
    testModulation (rig);
    testRandomizer (rig);
    testStateV2 (rig);
    testPerformance();
}
