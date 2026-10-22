//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "SBicSegmenter.h"

#include "AnalysisRun.h"

namespace audium {

std::vector<float> SBicSegmenter::analyze(const juce::File& audioFile)
{
    return analyze(audioFile, Parameters());
}

std::vector<float> SBicSegmenter::analyze(const juce::File& audioFile,
                                          const Parameters& params,
                                          const std::atomic<bool>* shouldAbort)
{
    AnalysisRun::Parameters runParams;
    runParams.segmentation = params;
    const AnalysisRun run;
    auto result = run.analyze(audioFile, { AnalysisType::Segments }, runParams, shouldAbort);
    return result.segments ? std::move(*result.segments) : std::vector<float>{};
}

} // namespace audium
