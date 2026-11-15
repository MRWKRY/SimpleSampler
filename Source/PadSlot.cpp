#include "PadSlot.h"
#include "ParamMath.h"

void SampleData::buildPeaks()
{
    peakMin.assign ((size_t) numPeaks, 0.0f);
    peakMax.assign ((size_t) numPeaks, 0.0f);

    const int n = buffer.getNumSamples();
    if (n <= 0 || buffer.getNumChannels() < 2)
        return;

    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getReadPointer (1);

    for (int b = 0; b < numPeaks; ++b)
    {
        const auto start = (int) ((juce::int64) b * n / numPeaks);
        const auto end   = juce::jmax (start + 1, (int) ((juce::int64) (b + 1) * n / numPeaks));
        float mn = 0.0f, mx = 0.0f;

        // for very long files look at a subset of the samples in each bucket
        const int step = juce::jmax (1, (end - start) / 512);
        for (int i = start; i < end && i < n; i += step)
        {
            const float v = 0.5f * (l[i] + r[i]);
            mn = juce::jmin (mn, v);
            mx = juce::jmax (mx, v);
        }
        peakMin[(size_t) b] = mn;
        peakMax[(size_t) b] = mx;
    }
}

//==============================================================================
void PadSlot::attachParameters (juce::AudioProcessorValueTreeState& apvts, int padIndex)
{
    using namespace ParamMath;
    auto get = [&] (const char* name) { return apvts.getRawParameterValue (id::pad (padIndex, name)); };

    note      = get (id::note);
    srcBpm    = get (id::srcBpm);
    dstBpm    = get (id::dstBpm);
    srcPitch  = get (id::srcPitch);
    semitones = get (id::semitones);
    loop      = get (id::loop);
    loopStart = get (id::loopStart);
    loopEnd   = get (id::loopEnd);
    attack    = get (id::attack);
    decay     = get (id::decay);
    sustain   = get (id::sustain);
    release   = get (id::release);

    jassert (note && srcBpm && dstBpm && srcPitch && semitones && loop
             && loopStart && loopEnd && attack && decay && sustain && release);
}

PadSettings PadSlot::readSettings() const noexcept
{
    using namespace ParamMath;
    PadSettings s;
    s.speedRatio = stretchRatio (srcBpm->load(), dstBpm->load());
    s.semitones  = (double) clampSemitones (juce::roundToInt (semitones->load()));
    s.loop       = loop->load() > 0.5f;

    const double a = juce::jlimit (0.0, 1.0, (double) loopStart->load());
    const double b = juce::jlimit (0.0, 1.0, (double) loopEnd->load());
    s.loopStart  = juce::jmin (a, b);
    s.loopEnd    = juce::jmax (a, b);

    s.attackSec  = juce::jmax (minAttackMs,  attack->load())  * 0.001f;
    s.decaySec   = juce::jmax (minDecayMs,   decay->load())   * 0.001f;
    s.sustain    = juce::jlimit (0.0f, 1.0f, sustain->load());
    s.releaseSec = juce::jmax (minReleaseMs, release->load()) * 0.001f;
    return s;
}

SampleData::Ptr PadSlot::getSampleForAudio() const noexcept
{
    const juce::SpinLock::ScopedTryLockType lock (sampleLock);
    if (lock.isLocked())
        return sample;
    return {};
}

SampleData::Ptr PadSlot::getSample() const
{
    const juce::SpinLock::ScopedLockType lock (sampleLock);
    return sample;
}

void PadSlot::setSample (SampleData::Ptr newSample)
{
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        std::swap (sample, newSample);
    }
    ++contentVersion;
}

void PadSlot::setFile (const juce::File& f, const juce::String& fileNameIfMissing)
{
    {
        const juce::ScopedLock lock (fileLock);
        file = f;
        fileName = f != juce::File() ? f.getFileName() : fileNameIfMissing;
        if (fileName.isEmpty())
            fileName = fileNameIfMissing;
    }
    ++contentVersion;
}

juce::File PadSlot::getFile() const
{
    const juce::ScopedLock lock (fileLock);
    return file;
}

juce::String PadSlot::getFileName() const
{
    const juce::ScopedLock lock (fileLock);
    return fileName;
}

bool PadSlot::hasFile() const
{
    const juce::ScopedLock lock (fileLock);
    return file != juce::File() || fileName.isNotEmpty();
}
