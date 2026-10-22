//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "AnalysisAudioLoader.h"
#include "AnalysisAbort.h"

#include <algorithm>
#include <cmath>

namespace audium {

namespace {

constexpr int blockSize = 32768;

// Pulls the reader's channels, averaged into one, as an AudioSource so the
// resampler can consume them. Past the end it yields silence.
class MonoReaderSource final : public juce::AudioSource {
public:
    explicit MonoReaderSource(juce::AudioFormatReader& readerToUse) : reader(readerToUse) {}

    void prepareToPlay(int samplesPerBlockExpected, double) override
    {
        scratch.setSize((int) reader.numChannels, samplesPerBlockExpected);
    }

    void releaseResources() override {}

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
    {
        info.clearActiveBufferRegion();

        const auto remaining = reader.lengthInSamples - position;
        const auto numToRead = (int) std::min<juce::int64>(info.numSamples, std::max<juce::int64>(0, remaining));
        if (numToRead <= 0)
            return;

        if (scratch.getNumSamples() < numToRead)
            scratch.setSize(scratch.getNumChannels(), numToRead, false, false, true);

        reader.read(scratch.getArrayOfWritePointers(), scratch.getNumChannels(), position, numToRead);
        position += numToRead;

        auto* out = info.buffer->getWritePointer(0, info.startSample);
        const auto gain = 1.0f / (float) scratch.getNumChannels();
        for (int channel = 0; channel < scratch.getNumChannels(); ++channel)
        {
            const auto* in = scratch.getReadPointer(channel);
            for (int i = 0; i < numToRead; ++i)
                out[i] += in[i] * gain;
        }
    }

private:
    juce::AudioFormatReader& reader;
    juce::int64 position = 0;
    juce::AudioBuffer<float> scratch;
};

} // namespace

std::optional<MonoAudio> AnalysisAudioLoader::load(const juce::File& audioFile) const
{
    return load(audioFile, Parameters());
}

juce::String AnalysisAudioLoader::describeProblem(const juce::File& audioFile)
{
    if (! audioFile.existsAsFile())
        return "file not found";

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(audioFile));
    if (reader == nullptr)
        return "no decoder for this format on this platform (readable: "
             + formats.getWildcardForAllFormats().replace("*", "").replace(";", " ") + ")";
    if (reader->numChannels == 0 || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return "the file holds no audio";
    return {};
}

std::optional<MonoAudio> AnalysisAudioLoader::load(const juce::File& audioFile,
                                                   const Parameters& params,
                                                   const std::atomic<bool>* shouldAbort) const
{
    if (! audioFile.existsAsFile() || params.targetSampleRate <= 0.0)
        return std::nullopt;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(audioFile));
    if (reader == nullptr || reader->numChannels == 0 || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return std::nullopt;

    MonoAudio result;
    result.sampleRate = params.targetSampleRate;

    // samples in per sample out; 1 when the file is already at the analysis rate
    const auto ratio = reader->sampleRate / params.targetSampleRate;
    const auto expectedLength = (size_t) std::max<juce::int64>(1, (juce::int64) std::floor((double) reader->lengthInSamples / ratio));
    result.samples.reserve(expectedLength);

    MonoReaderSource mono(*reader);
    juce::ResamplingAudioSource resampler(&mono, false, 1);
    const auto resample = std::abs(ratio - 1.0) > 1.0e-9;

    juce::AudioSource& source = resample ? static_cast<juce::AudioSource&>(resampler)
                                         : static_cast<juce::AudioSource&>(mono);
    if (resample)
        resampler.setResamplingRatio(ratio);
    source.prepareToPlay(blockSize, params.targetSampleRate);

    juce::AudioBuffer<float> block(1, blockSize);
    while (result.samples.size() < expectedLength)
    {
        if (isAborted(shouldAbort))
            return std::nullopt;

        const auto numSamples = (int) std::min<size_t>((size_t) blockSize, expectedLength - result.samples.size());
        juce::AudioSourceChannelInfo info(&block, 0, numSamples);
        source.getNextAudioBlock(info);

        const auto* data = block.getReadPointer(0);
        result.samples.insert(result.samples.end(), data, data + numSamples);
    }
    source.releaseResources();

    if (params.removeDcOffset)
    {
        double sum = 0.0;
        for (auto sample : result.samples)
            sum += sample;
        const auto mean = (float) (sum / (double) result.samples.size());
        for (auto& sample : result.samples)
            sample -= mean;
    }

    if (params.normalisePeak)
    {
        float peak = 0.0f;
        for (auto sample : result.samples)
            peak = std::max(peak, std::abs(sample));

        // Digital silence stays silent rather than becoming amplified noise.
        if (peak > 1.0e-9f)
        {
            const auto gain = 1.0f / peak;
            for (auto& sample : result.samples)
                sample *= gain;
        }
    }

    return result;
}

} // namespace audium
