//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "OnsetSegmenter.h"

#include "AnalysisRun.h"

namespace audium {

std::vector<float> OnsetSegmenter::analyze(const juce::File& audioFile)
{
    return analyze(audioFile, Parameters());
}

std::vector<float> OnsetSegmenter::analyze(const juce::File& audioFile,
                                           const Parameters& params,
                                           const std::atomic<bool>* shouldAbort)
{
    AnalysisRun::Parameters runParams;
    runParams.segmentation.sampleRate = params.sampleRate;
    runParams.onsets = params;
    const AnalysisRun run;
    auto result = run.analyze(audioFile, { AnalysisType::Onsets }, runParams, shouldAbort);
    return result.onsets ? std::move(*result.onsets) : std::vector<float>{};
}

} // namespace audium
