//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "BeatSegmenter.h"

#include "AnalysisRun.h"

namespace audium {

BeatSegmenter::Result BeatSegmenter::analyze(const juce::File& audioFile)
{
    return analyze(audioFile, Parameters());
}

BeatSegmenter::Result BeatSegmenter::analyze(const juce::File& audioFile,
                                             const Parameters& params,
                                             const std::atomic<bool>* shouldAbort)
{
    AnalysisRun::Parameters runParams;
    runParams.segmentation.sampleRate = params.sampleRate;
    runParams.beats = params;
    const AnalysisRun run;
    auto result = run.analyze(audioFile, { AnalysisType::Beats }, runParams, shouldAbort);
    return result.beats ? std::move(*result.beats) : Result{};
}

} // namespace audium
