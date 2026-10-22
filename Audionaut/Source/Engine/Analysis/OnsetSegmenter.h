//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once

#include <atomic>
#include <vector>
#include <JuceHeader.h>

namespace audium {

/**
 * @class OnsetSegmenter
 * @brief Detects note and percussive onsets in an audio file.
 *
 * The chain is AnalysisAudioLoader -> Stft -> OnsetStrength -> peak picking,
 * run by AnalysisRun, which shares the front end with the other analyses.
 * This class is the single-type door to it: a file in, onset timestamps out.
 */
class OnsetSegmenter {

public:
    /**
     * @brief Tunable parameters for the onset detection.
     */
    struct Parameters {
        /// The audio is resampled to this rate and analysed in 2048-sample
        /// frames with a hop of 512.
        float sampleRate = 22050.0f;
    };

    OnsetSegmenter() = default;

    /**
     * @brief Detects onsets using the default parameters.
     * @param audioFile The audio file to analyse.
     * @return Onset timestamps in seconds. Empty on failure.
     */
    std::vector<float> analyze(const juce::File& audioFile);

    /**
     * @brief Detects onsets for the given audio file.
     * @param audioFile The audio file to analyse.
     * @param params Onset-detection parameters.
     * @param shouldAbort Optional flag polled between the analysis stages
     *        (decoding, the STFT frames). Once set, the analysis gives up at
     *        the next poll and returns empty, so a caller shutting down (see
     *        AnalysisWorker) waits for one stage at most rather than for the
     *        whole file.
     * @return Onset timestamps in seconds. Empty on failure or when aborted.
     */
    std::vector<float> analyze(const juce::File& audioFile,
                               const Parameters& params,
                               const std::atomic<bool>* shouldAbort = nullptr);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OnsetSegmenter)
};

} // namespace audium
