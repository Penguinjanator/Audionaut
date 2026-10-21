//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "OnsetSegmenter.h"

#include "AnalysisAudioLoader.h"
#include "Stft.h"
#include "OnsetStrength.h"

namespace audium {

std::vector<float> OnsetSegmenter::analyze(const juce::File& audioFile)
{
    return analyze(audioFile, Parameters());
}

std::vector<float> OnsetSegmenter::analyze(const juce::File& audioFile,
                                           const Parameters& params,
                                           const std::atomic<bool>* shouldAbort)
{
    AnalysisAudioLoader loader;
    AnalysisAudioLoader::Parameters loaderParams;
    loaderParams.targetSampleRate = params.sampleRate;
    const auto audio = loader.load(audioFile, loaderParams, shouldAbort);
    if (! audio)
        return {};

    const Stft stft;
    const auto framesPerSecond = audio->sampleRate / (double) stft.getParameters().hopSize;

    OnsetStrength onsetStrength(OnsetStrength::Parameters(), stft.numBins(), audio->sampleRate);
    std::vector<float> envelope;
    envelope.reserve((size_t) stft.numFrames(audio->samples.size()));
    const auto completed = stft.forEachPowerFrame(audio->samples,
                                                  [&](int, const float* power)
                                                  {
                                                      envelope.push_back(onsetStrength.next(power));
                                                  },
                                                  shouldAbort);
    if (! completed)
        return {};

    std::vector<float> timestamps;
    for (auto frame : OnsetStrength::pickPeaks(envelope, framesPerSecond))
        timestamps.push_back((float) ((double) frame / framesPerSecond));
    return timestamps;
}

} // namespace audium
