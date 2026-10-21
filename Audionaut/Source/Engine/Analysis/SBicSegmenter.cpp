//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "SBicSegmenter.h"

#include "AnalysisAudioLoader.h"
#include "Stft.h"
#include "Mfcc.h"
#include "BicSegmenter.h"

namespace audium {

std::vector<float> SBicSegmenter::analyze(const juce::File& audioFile)
{
    return analyze(audioFile, Parameters());
}

std::vector<float> SBicSegmenter::analyze(const juce::File& audioFile,
                                          const Parameters& params,
                                          const std::atomic<bool>* shouldAbort)
{
    AnalysisAudioLoader loader;
    AnalysisAudioLoader::Parameters loaderParams;
    loaderParams.targetSampleRate = params.sampleRate;
    const auto audio = loader.load(audioFile, loaderParams, shouldAbort);
    if (! audio)
        return {};

    Stft::Parameters stftParams;
    stftParams.frameSize = params.frameSize;
    stftParams.hopSize = params.hopSize;
    const Stft stft(stftParams);

    Mfcc mfcc(Mfcc::Parameters(), stft.numBins(), audio->sampleRate);

    std::vector<std::vector<float>> features((size_t) stft.numFrames(audio->samples.size()),
                                             std::vector<float>((size_t) mfcc.getNumCoefficients()));
    const auto completed = stft.forEachPowerFrame(audio->samples,
                                                  [&](int frame, const float* power)
                                                  {
                                                      mfcc.compute(power, features[(size_t) frame].data());
                                                  },
                                                  shouldAbort);
    if (! completed)
        return {};

    BicSegmenter::Parameters bicParams;
    bicParams.size1 = params.size1;
    bicParams.inc1 = params.inc1;
    bicParams.size2 = params.size2;
    bicParams.inc2 = params.inc2;
    bicParams.minLength = params.minimumSegmentLength;
    bicParams.complexityPenaltyWeight = params.complexityPenaltyWeight;
    bicParams.correctSmallSampleBias = params.correctSmallSampleBias;

    const BicSegmenter segmenter;
    const auto boundaries = segmenter.segment(features, bicParams, shouldAbort);

    std::vector<float> timestamps;
    timestamps.reserve(boundaries.size());
    for (auto boundary : boundaries)
        timestamps.push_back((float) ((double) boundary * (double) params.hopSize / (double) params.sampleRate));
    return timestamps;
}

} // namespace audium
