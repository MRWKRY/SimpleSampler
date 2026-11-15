#include "SampleLoader.h"

SampleLoader::SampleLoader()
{
    formatManager.registerBasicFormats();   // WAV, AIFF, FLAC, OGG
}

SampleLoader::~SampleLoader()
{
    cancelAll();
}

void SampleLoader::runAsync (std::function<void()> job)
{
    pool.addJob (std::move (job));
}

void SampleLoader::cancelAll()
{
    pool.removeAllJobs (true, 20000);
}

bool SampleLoader::canRead (const juce::File& file) const
{
    if (! file.existsAsFile())
        return false;

    const auto ext = file.getFileExtension().toLowerCase();
    if (ext.isEmpty())
        return false;

    for (int i = 0; i < formatManager.getNumKnownFormats(); ++i)
        if (formatManager.getKnownFormat (i)->getFileExtensions().contains (ext, true))
            return true;

    return false;
}

SampleData::Ptr SampleLoader::loadFile (const juce::File& file, double targetRate, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "File not found: " + file.getFullPathName();
        return {};
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->numChannels == 0)
    {
        error = "Unsupported audio file: " + file.getFileName();
        return {};
    }

    const auto maxFrames = (juce::int64) (maxLengthSeconds * reader->sampleRate);
    const int numFrames = (int) juce::jmin (reader->lengthInSamples, maxFrames);
    if (numFrames <= 0)
    {
        error = "Empty audio file: " + file.getFileName();
        return {};
    }

    const int srcChannels = juce::jmin (2, (int) reader->numChannels);
    juce::AudioBuffer<float> source (srcChannels, numFrames);
    source.clear();
    if (! reader->read (&source, 0, numFrames, 0, true, srcChannels > 1))
    {
        error = "Could not read: " + file.getFileName();
        return {};
    }

    SampleData::Ptr data = new SampleData();
    data->name = file.getFileNameWithoutExtension();
    data->sourceSampleRate = reader->sampleRate;
    data->sourceChannels = (int) reader->numChannels;

    const bool resample = targetRate > 0.0 && std::abs (targetRate - reader->sampleRate) > 0.5;
    data->sampleRate = resample ? targetRate : reader->sampleRate;

    if (resample)
    {
        const double ratio = reader->sampleRate / targetRate;
        const int outFrames = juce::jmax (1, (int) std::floor ((double) numFrames / ratio));
        data->buffer.setSize (2, outFrames);
        data->buffer.clear();

        // The interpolator delays the signal by a fixed number of input samples:
        // feed some extra silence and drop the delayed start so the audio stays aligned.
        const int latencyIn = (int) std::ceil (juce::WindowedSincInterpolator::getBaseLatency());
        const int skipOut   = juce::roundToInt (juce::WindowedSincInterpolator::getBaseLatency() / ratio);
        const int paddedIn  = numFrames + latencyIn + 16;

        juce::HeapBlock<float> in (paddedIn, true);
        juce::HeapBlock<float> out (outFrames + skipOut, true);

        for (int ch = 0; ch < srcChannels; ++ch)
        {
            juce::FloatVectorOperations::copy (in.get(), source.getReadPointer (ch), numFrames);
            juce::WindowedSincInterpolator interpolator;
            interpolator.process (ratio, in.get(), out.get(), outFrames + skipOut, paddedIn, 0);
            data->buffer.copyFrom (ch, 0, out.get() + skipOut, outFrames);
        }
    }
    else
    {
        data->buffer.setSize (2, numFrames);
        for (int ch = 0; ch < srcChannels; ++ch)
            data->buffer.copyFrom (ch, 0, source, ch, 0, numFrames);
    }

    if (srcChannels == 1)   // mono -> same signal on both channels
        data->buffer.copyFrom (1, 0, data->buffer, 0, 0, data->buffer.getNumSamples());

    data->lengthSeconds = data->buffer.getNumSamples() / data->sampleRate;
    data->buildPeaks();
    return data;
}
