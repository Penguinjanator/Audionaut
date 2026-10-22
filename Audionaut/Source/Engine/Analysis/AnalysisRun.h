//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <vector>
#include <JuceHeader.h>

#include "AnalysisCache.h"
#include "SBicSegmenter.h"
#include "BeatSegmenter.h"
#include "OnsetSegmenter.h"

namespace audium {

/**
 * @class AnalysisRun
 * @brief One pass over an audio file that produces any of the analyses.
 *
 * The three analyses share their front end - decode, resample, STFT, and
 * for beats and onsets the onset strength envelopes - so asking for all of
 * them costs one decode and one STFT, not three. The pass computes only
 * what the requested types need: MFCC frames for the segmentation, the
 * envelopes for beats and onsets.
 *
 * The segmenters (SBicSegmenter, BeatSegmenter, OnsetSegmenter) are the
 * single-type doors to this; AnalysisProvider runs it for whatever a file
 * still lacks.
 */
class AnalysisRun {

public:
    struct Parameters {
        /// Also the pass's sample rate and STFT: the beat and onset
        /// parameters' sample rates are expected to match this one.
        SBicSegmenter::Parameters segmentation;
        BeatSegmenter::Parameters beats;
        OnsetSegmenter::Parameters onsets;
    };

    /// What came out; each analysis is set only when it was asked for and
    /// the pass completed.
    struct Result {
        std::optional<std::vector<float>> segments;
        std::optional<BeatSegmenter::Result> beats;
        std::optional<std::vector<float>> onsets;
    };

    /// Told how far the pass has got, 0 to 1, from the thread running it:
    /// after the decode, along the STFT, and after each analysis.
    using ProgressFn = std::function<void(double fraction)>;

    AnalysisRun() = default;

    /**
     * @brief Runs the requested analyses over the file.
     * @param audioFile The file to analyse.
     * @param types Which analyses to produce; duplicates are fine.
     * @param params Parameters of each analysis.
     * @param shouldAbort Optional flag polled between stages. Once set, the
     *        pass gives up and the result holds nothing.
     * @param progress Optional progress callback.
     * @return The analyses asked for. All empty when the file cannot be
     *         decoded or the pass was abandoned.
     */
    Result analyze(const juce::File& audioFile,
                   const std::vector<AnalysisType>& types,
                   const Parameters& params,
                   const std::atomic<bool>* shouldAbort = nullptr,
                   const ProgressFn& progress = {}) const;

    /// Runs with the default parameters.
    Result analyze(const juce::File& audioFile,
                   const std::vector<AnalysisType>& types,
                   const std::atomic<bool>* shouldAbort = nullptr,
                   const ProgressFn& progress = {}) const;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisRun)
};

} // namespace audium
