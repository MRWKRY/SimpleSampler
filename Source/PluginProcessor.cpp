#include "PluginProcessor.h"

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
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SimpleSamplerProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto msRange = [] (float minMs)
    {
        NormalisableRange<float> r (minMs, maxEnvMs, 0.1f);
        r.setSkewForCentre (200.0f);
        return r;
    };

    for (int i = 0; i < numPads; ++i)
    {
        const String prefix = "Pad " + String (i + 1) + " ";
        auto pid = [i] (const char* name) { return ParameterID { id::pad (i, name), 1 }; };

        auto group = std::make_unique<AudioProcessorParameterGroup> ("pad" + String (i + 1), "Pad " + String (i + 1), " | ");

        group->addChild (std::make_unique<AudioParameterInt> (pid (id::note), prefix + "MIDI Note", 0, 127, 36 + i,
                             AudioParameterIntAttributes().withAutomatable (false)
                                 .withStringFromValueFunction ([] (int v, int) { return noteName (v); })
                                 .withValueFromStringFunction ([] (const String& t) { const int n = parseNote (t); return n < 0 ? 36 : n; })));

        group->addChild (std::make_unique<AudioParameterFloat> (pid (id::srcBpm), prefix + "Start BPM",
                             NormalisableRange<float> (minBpm, maxBpm, 0.01f), 120.0f,
                             AudioParameterFloatAttributes().withLabel ("BPM")));
        group->addChild (std::make_unique<AudioParameterFloat> (pid (id::dstBpm), prefix + "Result BPM",
                             NormalisableRange<float> (minBpm, maxBpm, 0.01f), 120.0f,
                             AudioParameterFloatAttributes().withLabel ("BPM")));

        group->addChild (std::make_unique<AudioParameterInt> (pid (id::srcPitch), prefix + "Start Pitch", 0, 127, 60,
                             AudioParameterIntAttributes().withAutomatable (false)
                                 .withStringFromValueFunction ([] (int v, int) { return noteName (v); })
                                 .withValueFromStringFunction ([] (const String& t) { const int n = parseNote (t); return n < 0 ? 60 : n; })));
        group->addChild (std::make_unique<AudioParameterInt> (pid (id::semitones), prefix + "Pitch Shift",
                             -maxSemitones, maxSemitones, 0,
                             AudioParameterIntAttributes().withLabel ("st")));

        group->addChild (std::make_unique<AudioParameterBool>  (pid (id::loop), prefix + "Loop", false));
        group->addChild (std::make_unique<AudioParameterFloat> (pid (id::loopStart), prefix + "Loop Start",
                             NormalisableRange<float> (0.0f, 1.0f), 0.0f));
        group->addChild (std::make_unique<AudioParameterFloat> (pid (id::loopEnd), prefix + "Loop End",
                             NormalisableRange<float> (0.0f, 1.0f), 1.0f));

        group->addChild (std::make_unique<AudioParameterFloat> (pid (id::attack), prefix + "Attack",
                             msRange (minAttackMs), minAttackMs, AudioParameterFloatAttributes().withLabel ("ms")));
        group->addChild (std::make_unique<AudioParameterFloat> (pid (id::decay), prefix + "Decay",
                             msRange (minDecayMs), 100.0f, AudioParameterFloatAttributes().withLabel ("ms")));
        group->addChild (std::make_unique<AudioParameterFloat> (pid (id::sustain), prefix + "Sustain",
                             NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
        group->addChild (std::make_unique<AudioParameterFloat> (pid (id::release), prefix + "Release",
                             msRange (minReleaseMs), 50.0f, AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::move (group));
    }

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id::master, 1 }, "Output Volume",
                    NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f,
                    AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

//==============================================================================
SimpleSamplerProcessor::SimpleSamplerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createLayout())
{
    for (int i = 0; i < numPads; ++i)
        pads[(size_t) i].attachParameters (apvts, i);

    masterGainDb = apvts.getRawParameterValue (id::master);
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

    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (masterGainDb->load(), -60.0f));

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

    target->start (padIndex, midiNote, velocity, std::move (sample), pad.readSettings(), ++voiceCounter);
    pad.triggerCount.fetch_add (1);
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

void SimpleSamplerProcessor::renderVoices (juce::AudioBuffer<float>& buffer, int start, int num) noexcept
{
    if (num <= 0)
        return;

    float* l = buffer.getWritePointer (0) + start;
    float* r = buffer.getWritePointer (1) + start;

    for (auto& v : voices)
        if (v.isActive())
            v.render (l, r, num);
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

    int position = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, numSamples, meta.samplePosition);
        renderVoices (buffer, position, t - position);
        position = t;
        handleMidi (meta.getMessage());
    }
    renderVoices (buffer, position, numSamples - position);
    midi.clear();

    // output volume
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterGainDb->load(), -60.0f));
    if (masterGain.isSmoothing())
    {
        float* l = buffer.getWritePointer (0);
        float* r = buffer.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = masterGain.getNextValue();
            l[i] *= g;
            r[i] *= g;
        }
    }
    else
    {
        const float g = masterGain.getTargetValue();
        buffer.applyGain (0, 0, numSamples, g);
        buffer.applyGain (1, 0, numSamples, g);
    }

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
    root.setProperty (verProp, 1, nullptr);
    root.setProperty (nameProp, getPresetName(), nullptr);
    root.setProperty (selProp, getSelectedPad(), nullptr);
    root.setProperty ("editorWidth", editorWidth.load(), nullptr);
    root.setProperty ("editorHeight", editorHeight.load(), nullptr);

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
    return root;
}

bool SimpleSamplerProcessor::restoreStateTree (const juce::ValueTree& tree, const juce::File& searchDir)
{
    if (! tree.hasType (stateType))
        return false;

    const auto params = tree.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params.createCopy());

    setPresetName (tree.getProperty (nameProp, "Init").toString());
    setSelectedPad ((int) tree.getProperty (selProp, 0));
    editorWidth.store (juce::jlimit (400, 4000, (int) tree.getProperty ("editorWidth", editorWidth.load())));
    editorHeight.store (juce::jlimit (300, 3000, (int) tree.getProperty ("editorHeight", editorHeight.load())));

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

void SimpleSamplerProcessor::resetToInit()
{
    for (auto* p : getParameters())
        p->setValueNotifyingHost (p->getDefaultValue());

    for (int i = 0; i < numPads; ++i)
        clearPad (i);

    setPresetName ("Init");
    setSelectedPad (0);
    ++stateVersion;
}
