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
 * @class SBicSegmenter
 * @brief Segments an audio file into homogeneous regions: a BIC (Bayesian
 *        Information Criterion) segmentation over MFCC features.
 *
 * The chain is AnalysisAudioLoader -> Stft -> Mfcc -> BicSegmenter, all in
 * this directory. This class turns a file into boundary timestamps and is
 * what AnalysisProvider runs for AnalysisType::Segments.
 */
class SBicSegmenter {

public:
    /**
     * @brief Tunable parameters for the segmentation.
     *
     * The window values follow gaborgandalf, the Python prototype the auto
     * edit was ported from. They are given in frames: at 22050 Hz with a hop
     * of 512 that is 43.07 frames per second, so a minimum segment of 50
     * frames is about 1.16 s.
     *
     * The penalty is calibrated against what Essentia's SBic (which this
     * replaced) produced for the project's test recordings: 0.65 is where
     * the BIC here finds boundaries at the same density, with the
     * reference's boundaries almost all found and few extra. See
     * Catch2Tests/Tests/NativeSegmentationTests.cpp for the sweep.
     */
    struct Parameters {
        float sampleRate = 22050.0f;
        int   frameSize  = 2048;
        int   hopSize    = 512;

        // BIC segmentation window/increment sizes, in frames.
        int size1 = 200, inc1 = 60, size2 = 300, inc2 = 20;

        // Minimum segment length, in frames.
        int minimumSegmentLength = 50;

        // Complexity penalty weight [0, inf]. Lower yields more boundaries.
        float complexityPenaltyWeight = 0.65f;

        /// See BicSegmenter::Parameters.
        bool correctSmallSampleBias = true;
    };

    SBicSegmenter() = default;

    /**
     * @brief Computes segment boundaries using the default parameters.
     * @param audioFile The audio file to analyse.
     * @return Segment boundary timestamps in seconds, starting with 0 and
     *         ending with the file's duration. Empty on failure.
     */
    std::vector<float> analyze(const juce::File& audioFile);

    /**
     * @brief Computes segment boundaries for the given audio file.
     * @param audioFile The audio file to analyse.
     * @param params Segmentation parameters.
     * @param shouldAbort Optional flag polled between the analysis stages
     *        (decoding, the STFT frames, the BIC windows). Once set, the
     *        analysis gives up at the next poll and returns empty, so a
     *        caller shutting down (see AnalysisWorker) waits for one stage
     *        at most rather than for the whole file.
     * @return Segment boundary timestamps in seconds. Empty on failure or
     *         when aborted.
     */
    std::vector<float> analyze(const juce::File& audioFile,
                               const Parameters& params,
                               const std::atomic<bool>* shouldAbort = nullptr);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SBicSegmenter)
};

} // namespace audium
