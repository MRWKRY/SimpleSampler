#include "PluginProcessor.h"
#include <set>

using namespace ParamMath;

const juce::Identifier SimpleSamplerProcessor::stateType { "SimpleSamplerState" };

namespace
{
    const juce::Identifier padsType   { "Pads" };
    const juce::Identifier padType    { "Pad" };
    const juce::Identifier indexProp  { "index" };
    const juce::Identifier pathProp   { "path" };
    const juce::Identifier fileProp   { "fileName" };
    const juce::Identifier nameProp   { "presetName" };
    const juce::Identifier selProp    { "selectedPad" };
    const juce::Identifier verProp    { "version" };
    const juce::Identifier idProp     { "id" };

    // ---- value <-> text helpers for the host's parameter display ----------------
    using Attr = juce::AudioParameterFloatAttributes;

    Attr percentAttr()
    {
        return Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; });
    }
    Attr dbAttr (bool silentAtMinimum)
    {
        return Attr().withLabel ("dB").withStringFromValueFunction ([silentAtMinimum] (float v, int)
        {
            if (silentAtMinimum && v <= -59.9f) return juce::String ("-inf dB");
            return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
        });
    }
    Attr hzAttr()
    {
        return Attr().withLabel ("Hz").withStringFromValueFunction ([] (float v, int)
        {
            return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz" : juce::String (v / 1000.0f, 2) + " kHz";
        });
    }
    Attr msAttr()
    {
        return Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int)
        {
            return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " ms" : juce::String (v / 1000.0f, 2) + " s";
        });
    }
    Attr secondsAttr()
    {
        return Attr().withLabel ("s").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " s"; });
    }
    Attr semitoneAttr()
    {
        return Attr().withLabel ("st").withStringFromValueFunction ([] (float v, int) { return signedText (v) + " st"; });
    }

    juce::NormalisableRange<float> skewed (float lo, float hi, float step, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    }

    juce::ParameterID pid (const juce::String& id, int version)   { return { id, version }; }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SimpleSamplerProcessor::createLayout()
{
    using namespace juce;
    using Float = AudioParameterFloat;
    AudioProcessorValueTreeState::ParameterLayout layout;

    const NormalisableRange<float> percent (0.0f, 100.0f, 0.1f);

    // ------------------------------------------------------------------ pads
    for (int i = 0; i < numPads; ++i)
    {
        const String number = String (i + 1).paddedLeft ('0', 2);
        const String prefix = "Pad" + number + " ";
        auto p1 = [i] (const char* name) { return ParameterID { id::pad (i, name), 1 }; };   // since version 1
        auto p2 = [i] (const char* name) { return ParameterID { id::pad (i, name), 2 }; };   // since version 2

        auto group = std::make_unique<AudioProcessorParameterGroup> ("pad" + String (i + 1), "Pad " + number, " | ");

        group->addChild (std::make_unique<AudioParameterInt> (p1 (id::note), prefix + "MIDI Note", 0, 127, 36 + i,
                             AudioParameterIntAttributes().withAutomatable (false)
                                 .withStringFromValueFunction ([] (int v, int) { return noteName (v); })
                                 .withValueFromStringFunction ([] (const String& t) { const int n = parseNote (t); return n < 0 ? 36 : n; })));

        group->addChild (std::make_unique<Float> (p1 (id::srcBpm), prefix + "Start BPM",
                             NormalisableRange<float> (minBpm, maxBpm, 0.01f), 120.0f, Attr().withLabel ("BPM")));
        group->addChild (std::make_unique<Float> (p1 (id::dstBpm), prefix + "Result BPM",
                             NormalisableRange<float> (minBpm, maxBpm, 0.01f), 120.0f, Attr().withLabel ("BPM")));

        group->addChild (std::make_unique<AudioParameterInt> (p1 (id::srcPitch), prefix + "Start Pitch", 0, 127, 60,
                             AudioParameterIntAttributes().withAutomatable (false)
                                 .withStringFromValueFunction ([] (int v, int) { return noteName (v); })
                                 .withValueFromStringFunction ([] (const String& t) { const int n = parseNote (t); return n < 0 ? 60 : n; })));
        group->addChild (std::make_unique<AudioParameterInt> (p1 (id::semitones), prefix + "Pitch Shift",
                             -maxSemitones, maxSemitones, 0, AudioParameterIntAttributes().withLabel ("st")));

        group->addChild (std::make_unique<AudioParameterBool> (p1 (id::loop), prefix + "Loop", false));
        group->addChild (std::make_unique<Float> (p1 (id::loopStart), prefix + "Loop Start", NormalisableRange<float> (0.0f, 1.0f), 0.0f));
        group->addChild (std::make_unique<Float> (p1 (id::loopEnd), prefix + "Loop End", NormalisableRange<float> (0.0f, 1.0f), 1.0f));

        group->addChild (std::make_unique<Float> (p1 (id::attack), prefix + "Attack",
                             skewed (minAttackMs, maxEnvMs, 0.1f, 200.0f), minAttackMs, Attr().withLabel ("ms")));
        group->addChild (std::make_unique<Float> (p1 (id::decay), prefix + "Decay",
                             skewed (minDecayMs, maxEnvMs, 0.1f, 200.0f), 100.0f, Attr().withLabel ("ms")));
        group->addChild (std::make_unique<Float> (p1 (id::sustain), prefix + "Sustain",
                             NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
        group->addChild (std::make_unique<Float> (p1 (id::release), prefix + "Release",
                             skewed (minReleaseMs, maxEnvMs, 0.1f, 200.0f), 50.0f, Attr().withLabel ("ms")));

        // ---- version 2: mixer / tone
        group->addChild (std::make_unique<Float> (p2 (id::level), prefix + "Level", skewed (-60.0f, 6.0f, 0.1f, -12.0f), 0.0f, dbAttr (true)));
        group->addChild (std::make_unique<Float> (p2 (id::pan), prefix + "Pan", NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                             Attr().withStringFromValueFunction ([] (float v, int)
                             {
                                 const int n = roundToInt (v);
                                 return n == 0 ? String ("C") : String (std::abs (n)) + (n < 0 ? " L" : " R");
                             })));
        group->addChild (std::make_unique<Float> (p2 (id::eqLow), prefix + "EQ Low", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dbAttr (false)));
        group->addChild (std::make_unique<Float> (p2 (id::eqMid), prefix + "EQ Mid", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dbAttr (false)));
        group->addChild (std::make_unique<Float> (p2 (id::eqMidFreq), prefix + "EQ Mid Freq", skewed (200.0f, 8000.0f, 1.0f, 1000.0f), 1000.0f, hzAttr()));
        group->addChild (std::make_unique<Float> (p2 (id::eqHigh), prefix + "EQ High", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dbAttr (false)));
        group->addChild (std::make_unique<Float> (p2 (id::drive), prefix + "Drive", percent, 0.0f, percentAttr()));
        group->addChild (std::make_unique<Float> (p2 (id::sendRev), prefix + "Reverb Send", percent, 0.0f, percentAttr()));
        group->addChild (std::make_unique<Float> (p2 (id::sendDly), prefix + "Delay Send", percent, 0.0f, percentAttr()));

        // ---- version 2: genre knobs (0 = off)
        static const char* const genreIds[]   = { id::bloom, id::grain, id::drift, id::warmth, id::dew,
                                                  id::pump, id::wobble, id::gate, id::width, id::crush, id::dive, id::formant };
        static const char* const genreNames[] = { "Bloom", "Grain", "Drift", "Warmth", "Dew",
                                                  "Pump", "Wobble", "Gate", "Width", "Crush", "Dive", "Formant" };
        for (int k = 0; k < 12; ++k)
            group->addChild (std::make_unique<Float> (p2 (genreIds[k]), prefix + genreNames[k], percent, 0.0f, percentAttr()));

        layout.add (std::move (group));
    }

    layout.add (std::make_unique<Float> (ParameterID { id::master, 1 }, "Output Volume",
                    NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f, Attr().withLabel ("dB")));

    // ------------------------------------------------------------------ global FX
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("fx", "FX", " | ");
        group->addChild (std::make_unique<Float> (pid (id::rvSize, 2), "Reverb Size", percent, 50.0f, percentAttr()));
        group->addChild (std::make_unique<Float> (pid (id::rvDecay, 2), "Reverb Decay", skewed (0.2f, 12.0f, 0.01f, 2.5f), 2.5f, secondsAttr()));
        group->addChild (std::make_unique<Float> (pid (id::rvPreDelay, 2), "Reverb Pre-Delay", NormalisableRange<float> (0.0f, 200.0f, 0.1f), 20.0f, msAttr()));
        group->addChild (std::make_unique<Float> (pid (id::rvLowCut, 2), "Reverb Low Cut", skewed (20.0f, 1000.0f, 1.0f, 150.0f), 120.0f, hzAttr()));
        group->addChild (std::make_unique<Float> (pid (id::rvHighCut, 2), "Reverb High Cut", skewed (1000.0f, 20000.0f, 1.0f, 6000.0f), 9000.0f, hzAttr()));
        group->addChild (std::make_unique<Float> (pid (id::rvMix, 2), "Reverb Mix", percent, 70.0f, percentAttr()));

        group->addChild (std::make_unique<AudioParameterBool> (pid (id::dlSync, 2), "Delay Sync", true));
        group->addChild (std::make_unique<AudioParameterChoice> (pid (id::dlDivision, 2), "Delay Time (Sync)", TempoSync::divisionNames(), TempoSync::divEighthDotted));
        group->addChild (std::make_unique<Float> (pid (id::dlTime, 2), "Delay Time (Free)", skewed (1.0f, 2000.0f, 0.1f, 300.0f), 350.0f, msAttr()));
        group->addChild (std::make_unique<Float> (pid (id::dlFeedback, 2), "Delay Feedback", NormalisableRange<float> (0.0f, 95.0f, 0.1f), 40.0f, percentAttr()));
        group->addChild (std::make_unique<Float> (pid (id::dlLowCut, 2), "Delay Low Cut", skewed (20.0f, 2000.0f, 1.0f, 200.0f), 150.0f, hzAttr()));
        group->addChild (std::make_unique<Float> (pid (id::dlHighCut, 2), "Delay High Cut", skewed (500.0f, 20000.0f, 1.0f, 4000.0f), 6000.0f, hzAttr()));
        group->addChild (std::make_unique<AudioParameterBool> (pid (id::dlPingPong, 2), "Delay Ping-Pong", false));
        group->addChild (std::make_unique<Float> (pid (id::dlMix, 2), "Delay Mix", percent, 70.0f, percentAttr()));
        layout.add (std::move (group));
    }

    // ------------------------------------------------------------------ shared settings of the genre knobs
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("genre", "Genre", " | ");
        group->addChild (std::make_unique<AudioParameterChoice> (pid (id::pumpRate, 2), "Pump Rate", TempoSync::divisionNames(), TempoSync::divQuarter));
        group->addChild (std::make_unique<Float> (pid (id::pumpRelease, 2), "Pump Release", NormalisableRange<float> (5.0f, 100.0f, 0.1f), 60.0f, percentAttr()));
        group->addChild (std::make_unique<AudioParameterChoice> (pid (id::wobbleRate, 2), "Wobble Rate", TempoSync::divisionNames(), TempoSync::divEighth));
        group->addChild (std::make_unique<AudioParameterChoice> (pid (id::gateRate, 2), "Gate Step Length", TempoSync::divisionNames(), TempoSync::divSixteenth));

        static const bool defaultPattern[TempoSync::numSteps] = { 1,0,1,1, 1,0,1,1, 1,0,1,0, 1,1,1,0 };
        for (int s = 0; s < TempoSync::numSteps; ++s)
            group->addChild (std::make_unique<AudioParameterBool> (pid (id::gateStep (s), 2),
                                 "Gate Step " + String (s + 1).paddedLeft ('0', 2), defaultPattern[s]));

        group->addChild (std::make_unique<Float> (pid (id::diveRange, 2), "Dive Range", NormalisableRange<float> (-24.0f, 24.0f, 1.0f), -12.0f, semitoneAttr()));
        group->addChild (std::make_unique<Float> (pid (id::diveTime, 2), "Dive Time", skewed (20.0f, 2000.0f, 1.0f, 300.0f), 250.0f, msAttr()));
        group->addChild (std::make_unique<Float> (pid (id::formantRange, 2), "Formant Range", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 5.0f, semitoneAttr()));
        layout.add (std::move (group));
    }

    // ------------------------------------------------------------------ modulators, tempo, randomiser
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("mod", "Mod", " | ");
        static const int defaultShape[numMods] = { TempoSync::sine, TempoSync::triangle, TempoSync::smoothRandom, TempoSync::stepSeq };
        static const int defaultRate[numMods]  = { TempoSync::divQuarter, TempoSync::divBar, TempoSync::divEighth, TempoSync::divSixteenth };

        for (int m = 0; m < numMods; ++m)
        {
            const String prefix = "Mod" + String (m + 1) + " ";
            group->addChild (std::make_unique<AudioParameterChoice> (pid (id::mod (m, id::modType), 2), prefix + "Type", TempoSync::shapeNames(), defaultShape[m]));
            group->addChild (std::make_unique<AudioParameterBool> (pid (id::mod (m, id::modSync), 2), prefix + "Sync", true));
            group->addChild (std::make_unique<AudioParameterChoice> (pid (id::mod (m, id::modRate), 2), prefix + "Rate (Sync)", TempoSync::divisionNames(), defaultRate[m]));
            group->addChild (std::make_unique<Float> (pid (id::mod (m, id::modHz), 2), prefix + "Rate (Free)", skewed (0.02f, 20.0f, 0.001f, 1.0f), 1.0f,
                                 Attr().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " Hz"; })));
            group->addChild (std::make_unique<AudioParameterChoice> (pid (id::mod (m, id::modMode), 2), prefix + "Phase Mode", TempoSync::modeNames(), TempoSync::hostPosition));

            for (int s = 0; s < TempoSync::numSteps; ++s)
                group->addChild (std::make_unique<Float> (pid (id::modStep (m, s), 2), prefix + "Step " + String (s + 1).paddedLeft ('0', 2),
                                     NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 1.0f - 2.0f * (float) s / 15.0f));
        }

        group->addChild (std::make_unique<Float> (pid (id::tempo, 2), "Manual Tempo", NormalisableRange<float> (20.0f, 300.0f, 0.1f), 120.0f,
                             Attr().withLabel ("BPM").withAutomatable (false)));
        group->addChild (std::make_unique<Float> (pid (id::rndAmount, 2), "Randomize Amount", percent, 30.0f, percentAttr()));
        group->addChild (std::make_unique<AudioParameterChoice> (pid (id::rndScope, 2), "Randomize Scope", Randomizer::scopeNames(), Randomizer::padScope));
        group->addChild (std::make_unique<AudioParameterBool> (pid (id::rndAuto, 2), "Auto Randomize", false));
        group->addChild (std::make_unique<AudioParameterChoice> (pid (id::rndInterval, 2), "Auto Randomize Every", TempoSync::intervalNames(), 2));
        layout.add (std::move (group));
    }

    return layout;
}

//==============================================================================
SimpleSamplerProcessor::SimpleSamplerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createLayout())
{
    for (int i = 0; i < numPads; ++i)
        pads[(size_t) i].attachParameters (apvts, i);

    auto raw = [this] (const juce::String& paramID)
    {
        auto* v = apvts.getRawParameterValue (paramID);
        jassert (v != nullptr);
        return v;
    };

    masterGainDb  = raw (id::master);
    manualTempo   = raw (id::tempo);
    delaySync     = raw (id::dlSync);
    delayDivision = raw (id::dlDivision);
    delayPingPong = raw (id::dlPingPong);
    pumpRate      = raw (id::pumpRate);
    wobbleRate    = raw (id::wobbleRate);
    gateRate      = raw (id::gateRate);
    diveRange     = raw (id::diveRange);
    diveTime      = raw (id::diveTime);
    formantRange  = raw (id::formantRange);
    for (int s = 0; s < TempoSync::numSteps; ++s)
        gateSteps[s] = raw (id::gateStep (s));

    // ---- everything that can be modulated / randomised
    const auto* names = id::padFxNames();
    for (int p = 0; p < numPads; ++p)
        for (int k = 0; k < numPadFx; ++k)
            padTargets[p][k] = modMatrix.addTarget (apvts, id::pad (p, names[k]), p, k == id::fxLevel);   // Level starts locked for the randomiser

    static const char* const globalIds[numGlobalTargets] =
    {
        id::rvSize, id::rvDecay, id::rvPreDelay, id::rvLowCut, id::rvHighCut, id::rvMix,
        id::dlTime, id::dlFeedback, id::dlLowCut, id::dlHighCut, id::dlMix, id::pumpRelease
    };
    for (int g = 0; g < numGlobalTargets; ++g)
        globalTargets[g] = modMatrix.addTarget (apvts, globalIds[g], -1);

    modMatrix.finishSetup();

    for (int m = 0; m < numMods; ++m)
        modulators[(size_t) m].attach (apvts, m);

    randomizer = std::make_unique<Randomizer> (modMatrix, apvts);
}

SimpleSamplerProcessor::~SimpleSamplerProcessor()
{
    loader.cancelAll();
}

juce::File SimpleSamplerProcessor::getUserFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("SimpleSampler");
}

juce::RangedAudioParameter* SimpleSamplerProcessor::getPadParameter (int padIndex, const char* name) const
{
    return apvts.getParameter (id::pad (padIndex, name));
}

juce::RangedAudioParameter* SimpleSamplerProcessor::getMasterParameter() const
{
    return apvts.getParameter (id::master);
}

//==============================================================================
void SimpleSamplerProcessor::prepareToPlay (double sampleRate, int)
{
    currentRate.store (sampleRate);

    for (auto& v : voices)
        v.prepare (sampleRate);

    for (int i = 0; i < numPads; ++i)
        channels[(size_t) i].prepare (sampleRate, i);

    globalFx.prepare (sampleRate);
    modMatrix.prepare (sampleRate);
    for (auto& m : modulators)
        m.reset();

    // (re)load samples that are not at this sample rate yet
    for (int i = 0; i < numPads; ++i)
    {
        auto& pad = pads[(size_t) i];
        if (pad.getFile() == juce::File())
            continue;

        const auto s = pad.getSample();
        const bool needsLoad = (s == nullptr && ! pad.isMissing() && ! pad.loading.load())
                            || (s != nullptr && std::abs (s->sampleRate - sampleRate) > 0.5);
        if (needsLoad || pad.loading.load())
            queueLoad (i);
    }
}

void SimpleSamplerProcessor::releaseResources()
{
    for (auto& v : voices)
        v.stopNow();

    for (auto& c : channels)
        c.reset();
    globalFx.reset();

    for (auto& p : pads)
    {
        p.activeVoices.store (0);
        p.playPosition.store (-1.0f);
    }
    totalActiveVoices.store (0);
}

bool SimpleSamplerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
void SimpleSamplerProcessor::triggerPadFromUI (int padIndex, bool isDown) noexcept
{
    if (padIndex < 0 || padIndex >= numPads)
        return;

    const uint32_t bit = 1u << padIndex;
    if (isDown) uiNoteOn.fetch_or (bit);
    else        uiNoteOff.fetch_or (bit);
}

void SimpleSamplerProcessor::startPad (int padIndex, int midiNote, float velocity) noexcept
{
    auto& pad = pads[(size_t) padIndex];
    auto sample = pad.getSampleForAudio();
    if (sample == nullptr || sample->numSamples() <= 0
        || std::abs (sample->sampleRate - currentRate.load()) > 0.5)
        return;

    // polyphony limit: fade out the oldest voice
    int sounding = 0;
    Voice* oldest = nullptr;
    for (auto& v : voices)
    {
        if (v.isActive() && ! v.isBeingKilled())
        {
            ++sounding;
            if (oldest == nullptr || v.getStartOrder() < oldest->getStartOrder())
                oldest = &v;
        }
    }
    if (sounding >= maxVoices && oldest != nullptr)
        oldest->kill();

    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.isActive()) { target = &v; break; }

    if (target == nullptr)
    {
        // no spare voice: reuse the oldest one that is already fading out (or simply the oldest)
        for (auto& v : voices)
            if (v.isBeingKilled() && (target == nullptr || v.getStartOrder() < target->getStartOrder()))
                target = &v;

        if (target == nullptr)
            for (auto& v : voices)
                if (target == nullptr || v.getStartOrder() < target->getStartOrder())
                    target = &v;

        target->stopNow();
    }

    auto settings = pad.readSettings();

    // genre knobs that act inside the voice (values at note-on, including modulation)
    const float dive    = juce::jlimit (0.0f, 1.0f, modMatrix.get (padTargets[padIndex][id::fxDive]) * 0.01f);
    const float formant = juce::jlimit (0.0f, 1.0f, modMatrix.get (padTargets[padIndex][id::fxFormant]) * 0.01f);
    const float drift   = juce::jlimit (0.0f, 1.0f, modMatrix.get (padTargets[padIndex][id::fxDrift]) * 0.01f);
    settings.diveSemitones    = dive * diveRange->load();
    settings.diveSeconds      = diveTime->load() * 0.001f;
    settings.formantSemitones = formant * formantRange->load();
    settings.forceStretch     = dive > 0.0f || formant > 0.0f || drift > 0.0f;

    target->start (padIndex, midiNote, velocity, std::move (sample), settings, ++voiceCounter);
    pad.triggerCount.fetch_add (1);

    for (auto& m : modulators)
        m.noteOn();
}

void SimpleSamplerProcessor::releaseNote (int midiNote) noexcept
{
    // one-shot voices ignore note-off; only looping voices are released
    for (auto& v : voices)
        if (v.isActive() && v.getNote() == midiNote && v.isLoopVoice())
            v.noteOff();
}

void SimpleSamplerProcessor::killAllVoices() noexcept
{
    for (auto& v : voices)
        v.kill();
}

void SimpleSamplerProcessor::handleMidi (const juce::MidiMessage& m) noexcept
{
    if (m.isNoteOn())
    {
        const int note = m.getNoteNumber();
        for (int i = 0; i < numPads; ++i)
            if (pads[(size_t) i].getNote() == note)
                startPad (i, note, m.getFloatVelocity());
    }
    else if (m.isNoteOff())
    {
        releaseNote (m.getNoteNumber());
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        killAllVoices();
    }
}

/** Settings that are read once per block (not modulated). */
void SimpleSamplerProcessor::readBlockSettings() noexcept
{
    genre.pumpBeats     = TempoSync::divisionBeats (juce::roundToInt (pumpRate->load()));
    genre.wobbleBeats   = TempoSync::divisionBeats (juce::roundToInt (wobbleRate->load()));
    genre.gateStepBeats = TempoSync::divisionBeats (juce::roundToInt (gateRate->load()));
    for (int s = 0; s < TempoSync::numSteps; ++s)
        genre.gateSteps[s] = gateSteps[s]->load() > 0.5f;
}

/** Renders up to maxSegment samples: modulation -> voices -> pad channel strips -> send effects -> limiter. */
void SimpleSamplerProcessor::processSegment (juce::AudioBuffer<float>& buffer, int start, int n) noexcept
{
    const double sr = currentRate.load (std::memory_order_relaxed);
    const double beatBefore = clock.beat;
    const double beatAfter = beatBefore + (double) n * clock.bpm / (60.0 * sr);

    // ---- modulation for this segment
    float modValues[numMods];
    for (int m = 0; m < numMods; ++m)
        modValues[m] = modulators[(size_t) m].advance (n, clock, sr);

    randomizer->processAuto (beatBefore, beatAfter, selectedPad.load (std::memory_order_relaxed));
    modMatrix.update (modValues, n);
    genre.pumpRelease = modMatrix.get (globalTargets[gPumpRelease]) * 0.01f;

    // ---- which pads are sounding
    bool hasVoices[numPads] = {};
    for (auto& v : voices)
        if (v.isActive() && v.getPad() >= 0 && v.getPad() < numPads)
            hasVoices[v.getPad()] = true;

    float mainL[maxSegment] = {}, mainR[maxSegment] = {};
    float revL[maxSegment] = {}, revR[maxSegment] = {};
    float dlyL[maxSegment] = {}, dlyR[maxSegment] = {};
    float bloomL[maxSegment] = {}, bloomR[maxSegment] = {};

    bool channelOn[numPads];
    fx::PadValues values[numPads];
    const float formantSemitones = formantRange->load (std::memory_order_relaxed);

    for (int p = 0; p < numPads; ++p)
    {
        channelOn[p] = channels[(size_t) p].update (hasVoices[p], n);
        if (! channelOn[p])
            continue;

        for (int k = 0; k < numPadFx; ++k)
            values[p].v[k] = modMatrix.get (padTargets[p][k]);

        channels[(size_t) p].advanceDrift (n, values[p].amount (id::fxDrift));
        formantNow[p] = values[p].amount (id::fxFormant) * formantSemitones;
        juce::FloatVectorOperations::clear (padBuffers[p][0], n);
        juce::FloatVectorOperations::clear (padBuffers[p][1], n);
    }

    // ---- voices render into their pad's bus
    for (auto& v : voices)
    {
        if (! v.isActive())
            continue;
        const int p = v.getPad();
        if (p >= 0 && p < numPads && channelOn[p])
            v.render (padBuffers[p][0], padBuffers[p][1], n, channels[(size_t) p].getDriftSemitones(), formantNow[p]);
    }

    // ---- channel strips
    const fx::MixBuses buses { mainL, mainR, revL, revR, dlyL, dlyR, bloomL, bloomR };
    const double beatMid = 0.5 * (beatBefore + beatAfter);
    for (int p = 0; p < numPads; ++p)
        if (channelOn[p])
            channels[(size_t) p].process (padBuffers[p][0], padBuffers[p][1], n, values[p], genre, beatMid, buses);

    // ---- send effects, output volume, limiter
    fx::GlobalValues g;
    g.rvSize     = modMatrix.get (globalTargets[gRvSize]);
    g.rvDecay    = modMatrix.get (globalTargets[gRvDecay]);
    g.rvPreDelay = modMatrix.get (globalTargets[gRvPreDelay]);
    g.rvLowCut   = modMatrix.get (globalTargets[gRvLowCut]);
    g.rvHighCut  = modMatrix.get (globalTargets[gRvHighCut]);
    g.rvMix      = modMatrix.get (globalTargets[gRvMix]);
    g.dlTimeMs   = delaySync->load() > 0.5f ? (float) TempoSync::divisionMs (juce::roundToInt (delayDivision->load()), clock.bpm)
                                            : modMatrix.get (globalTargets[gDlTime]);
    g.dlFeedback = modMatrix.get (globalTargets[gDlFeedback]);
    g.dlLowCut   = modMatrix.get (globalTargets[gDlLowCut]);
    g.dlHighCut  = modMatrix.get (globalTargets[gDlHighCut]);
    g.dlMix      = modMatrix.get (globalTargets[gDlMix]);
    g.dlPingPong = delayPingPong->load() > 0.5f;
    g.masterDb   = masterGainDb->load();

    globalFx.process (mainL, mainR, revL, revR, dlyL, dlyR, bloomL, bloomR, n, g);

    juce::FloatVectorOperations::copy (buffer.getWritePointer (0) + start, mainL, n);
    juce::FloatVectorOperations::copy (buffer.getWritePointer (1) + start, mainR, n);

    clock.beat = beatAfter;
}

void SimpleSamplerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    if (buffer.getNumChannels() < 2 || currentRate.load() <= 0.0)
    {
        midi.clear();
        return;
    }

    clock.beginBlock (getPlayHead(), (double) manualTempo->load());
    readBlockSettings();

    // pads clicked in the editor (note numbers below zero identify UI voices)
    const uint32_t on = uiNoteOn.exchange (0), off = uiNoteOff.exchange (0);
    if ((on | off) != 0)
    {
        for (int i = 0; i < numPads; ++i)
        {
            if (on  & (1u << i)) startPad (i, -(i + 1), 0.85f);
            if (off & (1u << i)) releaseNote (-(i + 1));
        }
    }

    // segments end at MIDI events (sample accurate) and after at most maxSegment samples (control rate)
    auto event = midi.begin();
    const auto lastEvent = midi.end();
    int position = 0;

    while (position < numSamples)
    {
        while (event != lastEvent && (*event).samplePosition <= position)
        {
            handleMidi ((*event).getMessage());
            ++event;
        }

        int end = juce::jmin (numSamples, position + maxSegment);
        if (event != lastEvent)
            end = juce::jmin (end, juce::jmax (position + 1, (*event).samplePosition));

        processSegment (buffer, position, end - position);
        position = end;
    }

    for (; event != lastEvent; ++event)
        handleMidi ((*event).getMessage());
    midi.clear();

    // feedback for the editor
    int counts[numPads] = {};
    juce::uint64 newest[numPads] = {};
    float positions[numPads];
    for (auto& p : positions) p = -1.0f;
    int total = 0;

    for (auto& v : voices)
    {
        if (! v.isActive())
            continue;

        ++total;
        const int p = v.getPad();
        if (p < 0 || p >= numPads)
            continue;

        ++counts[p];
        if (v.getStartOrder() >= newest[p])
        {
            newest[p] = v.getStartOrder();
            positions[p] = v.getNormalisedPosition();
        }
    }

    for (int i = 0; i < numPads; ++i)
    {
        pads[(size_t) i].activeVoices.store (counts[i], std::memory_order_relaxed);
        pads[(size_t) i].playPosition.store (positions[i], std::memory_order_relaxed);
    }
    totalActiveVoices.store (total, std::memory_order_relaxed);
    modMatrix.publishForUi();
    uiBpm.store (clock.bpm, std::memory_order_relaxed);
    uiHostTempo.store (clock.hostTempo, std::memory_order_relaxed);
}

//==============================================================================
void SimpleSamplerProcessor::retainSample (const SampleData::Ptr& s)
{
    const juce::ScopedLock lock (retainLock);
    retained.add (s);
}

void SimpleSamplerProcessor::pruneRetained()
{
    // A sample only referenced by this list can no longer be reached by the audio thread.
    const juce::ScopedLock lock (retainLock);
    for (int i = retained.size(); --i >= 0;)
        if (retained.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
            retained.remove (i);
}

void SimpleSamplerProcessor::queueLoad (int padIndex)
{
    auto& pad = pads[(size_t) padIndex];
    const int generation = ++pad.loadGeneration;
    const auto file = pad.getFile();
    pad.loading.store (true);
    ++pad.contentVersion;

    loader.runAsync ([this, padIndex, generation, file]
    {
        auto& p = pads[(size_t) padIndex];
        if (p.loadGeneration.load() != generation)
            return;

        juce::String error;
        auto sample = loader.loadFile (file, currentRate.load(), error);

        if (p.loadGeneration.load() != generation)
            return;

        if (sample != nullptr)
            retainSample (sample);

        p.setMissing (sample == nullptr);
        p.loading.store (false);
        p.setSample (sample);
        pruneRetained();
    });
}

void SimpleSamplerProcessor::loadSampleIntoPad (int padIndex, const juce::File& file, bool resetLoop)
{
    if (padIndex < 0 || padIndex >= numPads)
        return;

    auto& pad = pads[(size_t) padIndex];
    pad.setFile (file);
    pad.setMissing (false);

    if (resetLoop)
    {
        if (auto* p = getPadParameter (padIndex, id::loopStart)) p->setValueNotifyingHost (0.0f);
        if (auto* p = getPadParameter (padIndex, id::loopEnd))   p->setValueNotifyingHost (1.0f);
    }

    queueLoad (padIndex);
}

bool SimpleSamplerProcessor::loadSampleIntoPadBlocking (int padIndex, const juce::File& file)
{
    if (padIndex < 0 || padIndex >= numPads)
        return false;

    auto& pad = pads[(size_t) padIndex];
    ++pad.loadGeneration;
    pad.setFile (file);

    juce::String error;
    auto sample = loader.loadFile (file, currentRate.load(), error);
    if (sample != nullptr)
        retainSample (sample);

    pad.setMissing (sample == nullptr);
    pad.loading.store (false);
    pad.setSample (sample);
    pruneRetained();
    return sample != nullptr;
}

void SimpleSamplerProcessor::clearPad (int padIndex)
{
    if (padIndex < 0 || padIndex >= numPads)
        return;

    auto& pad = pads[(size_t) padIndex];
    ++pad.loadGeneration;
    pad.loading.store (false);
    pad.setMissing (false);
    pad.setFile ({});
    pad.setSample (nullptr);
    pruneRetained();
}

void SimpleSamplerProcessor::resetToInit()
{
    for (auto* p : getParameters())
        p->setValueNotifyingHost (p->getDefaultValue());

    for (int i = 0; i < numPads; ++i)
        clearPad (i);

    modMatrix.fromTree ({});
    randomizer->clearUndo();
    setPresetName ("Init");
    setSelectedPad (0);
    ++stateVersion;
}

//==============================================================================
juce::String SimpleSamplerProcessor::getPresetName() const
{
    const juce::ScopedLock lock (nameLock);
    return presetName;
}

void SimpleSamplerProcessor::setPresetName (const juce::String& name)
{
    const juce::ScopedLock lock (nameLock);
    presetName = name;
}

juce::ValueTree SimpleSamplerProcessor::createStateTree() const
{
    juce::ValueTree root (stateType);
    root.setProperty (verProp, currentStateVersion, nullptr);
    root.setProperty (nameProp, getPresetName(), nullptr);
    root.setProperty (selProp, getSelectedPad(), nullptr);
    root.setProperty ("editorWidth", editorWidth.load(), nullptr);
    root.setProperty ("editorHeight", editorHeight.load(), nullptr);
    root.setProperty ("editorTab", editorTab.load(), nullptr);

    root.appendChild (const_cast<juce::AudioProcessorValueTreeState&> (apvts).copyState(), nullptr);

    juce::ValueTree padsTree (padsType);
    for (int i = 0; i < numPads; ++i)
    {
        const auto& pad = pads[(size_t) i];
        if (! pad.hasFile())
            continue;

        juce::ValueTree t (padType);
        t.setProperty (indexProp, i, nullptr);
        t.setProperty (pathProp, pad.getFile().getFullPathName(), nullptr);   // absolute path
        t.setProperty (fileProp, pad.getFileName(), nullptr);                 // file name only (fallback when moved)
        padsTree.appendChild (t, nullptr);
    }
    root.appendChild (padsTree, nullptr);

    // version 2: modulation assignments and randomiser locks
    root.appendChild (modMatrix.toTree(), nullptr);
    return root;
}

bool SimpleSamplerProcessor::restoreStateTree (const juce::ValueTree& tree, const juce::File& searchDir)
{
    if (! tree.hasType (stateType))
        return false;

    // Any version is accepted. Whatever an older file does not contain keeps / gets its default:
    //   version 1 has no mixer, FX, genre, modulator or randomiser parameters and no "Modulation" child.
    const auto params = tree.getChildWithName (apvts.state.getType());
    if (params.isValid())
    {
        std::set<juce::String> present;
        for (const auto& child : params)
            present.insert (child.getProperty (idProp).toString());

        apvts.replaceState (params.createCopy());

        // replaceState() leaves parameters that are missing from the tree untouched, so reset those explicitly
        for (auto* p : getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (present.find (ranged->paramID) == present.end())
                    ranged->setValueNotifyingHost (ranged->getDefaultValue());
    }

    modMatrix.fromTree (tree.getChildWithName (ModMatrix::treeType));
    randomizer->clearUndo();

    setPresetName (tree.getProperty (nameProp, "Init").toString());
    setSelectedPad ((int) tree.getProperty (selProp, 0));
    editorWidth.store (juce::jlimit (400, 4000, (int) tree.getProperty ("editorWidth", editorWidth.load())));
    editorHeight.store (juce::jlimit (300, 3000, (int) tree.getProperty ("editorHeight", editorHeight.load())));
    editorTab.store (juce::jlimit (0, 3, (int) tree.getProperty ("editorTab", editorTab.load())));

    const auto padsTree = tree.getChildWithName (padsType);

    for (int i = 0; i < numPads; ++i)
    {
        juce::ValueTree entry;
        for (const auto& c : padsTree)
            if (c.hasType (padType) && (int) c.getProperty (indexProp, -1) == i)
                entry = c;

        const auto path = entry.getProperty (pathProp).toString();
        auto fileName   = entry.getProperty (fileProp).toString();

        if (! entry.isValid() || (path.isEmpty() && fileName.isEmpty()))
        {
            clearPad (i);
            continue;
        }

        juce::File original;
        if (juce::File::isAbsolutePath (path))
            original = juce::File (path);
        if (fileName.isEmpty() && original != juce::File())
            fileName = original.getFileName();

        // 1) absolute path, 2) same file name next to the preset, 3) the user sample folder
        juce::File found;
        if (original.existsAsFile())
        {
            found = original;
        }
        else if (fileName.isNotEmpty())
        {
            juce::Array<juce::File> folders;
            if (searchDir.isDirectory())
            {
                folders.add (searchDir);
                folders.add (searchDir.getChildFile ("Samples"));
            }
            folders.add (getUserFolder().getChildFile ("Samples"));
            folders.add (getUserFolder().getChildFile ("Presets"));

            for (const auto& f : folders)
            {
                const auto candidate = f.getChildFile (fileName);
                if (candidate.existsAsFile()) { found = candidate; break; }
            }
        }

        auto& pad = pads[(size_t) i];
        if (found != juce::File())
        {
            if (found == pad.getFile() && pad.getSample() != nullptr && ! pad.isMissing())
                continue;   // already loaded

            pad.setFile (found);
            pad.setMissing (false);
            queueLoad (i);
        }
        else
        {
            ++pad.loadGeneration;
            pad.loading.store (false);
            pad.setFile (original, fileName);
            pad.setMissing (true);
            pad.setSample (nullptr);
        }
    }

    ++stateVersion;
    return true;
}

void SimpleSamplerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = createStateTree().createXml())
        copyXmlToBinary (*xml, destData);
}

void SimpleSamplerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        restoreStateTree (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SimpleSamplerProcessor();
}
