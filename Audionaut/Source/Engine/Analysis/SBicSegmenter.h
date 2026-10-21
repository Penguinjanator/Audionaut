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
 * @brief Segments an audio file into homogeneous regions using Essentia's
 *        SBic (Bayesian Information Criterion) algorithm over MFCC features.
 *
 * The public interface is deliberately free of any Essentia types so this
 * header can be included from engine code (e.g. AnalysisProvider) that does
 * not itself link against Essentia. The signal chain
 * (MonoLoader -> FrameCutter -> Windowing -> Spectrum -> MFCC -> SBic) lives
 * entirely in the .cpp.
 */
class SBicSegmenter {

public:
    /**
     * @brief Tunable parameters for the segmentation.
     *
     * The BIC values follow gaborgandalf rather than Essentia's stock defaults,
     * which is what this used to carry. Measured against the reference on the
     * project's test material, they are what account for most of the difference
     * in where the two implementations cut: the lower complexity penalty in
     * particular roughly doubles the number of boundaries found, while the
     * feature the BIC runs over - MFCC here, chroma there - turns out to matter
     * comparatively little.
     *
     * Note the frame rate works out the same on both sides despite the
     * different rate and hop: 44100/1024 and 22050/512 are both 43.07 frames
     * per second, so the window and length values transfer directly.
     */
    struct Parameters {
        float sampleRate = 44100.0f;
        int   frameSize  = 2048;
        int   hopSize    = 1024;

        // BIC segmentation window/increment sizes, in frames.
        int size1 = 200, inc1 = 60, size2 = 300, inc2 = 20;

        // Minimum segment length, in frames. 50 is ~1.16 s at the frame rate
        // above.
        int minimumSegmentLength = 50;

        // Complexity penalty weight [0, inf]. Lower yields more boundaries.
        float complexityPenaltyWeight = 0.5f;
        /// Native implementation only: see BicSegmenter::Parameters.
        bool correctSmallSampleBias = true;
    };

    /**
     * @brief The parameters the native implementation is calibrated for.
     *
     * 22050 Hz with a hop of 512: the same 43.07 frames per second as the
     * Essentia chain, so the window and length values are the ones above.
     * The penalty is 0.65 rather than 0.5: held against what Essentia's SBic
     * produced for the project's test recordings, that is where the native
     * BIC finds boundaries at the same density, with the reference's
     * boundaries almost all found and few extra. See
     * Catch2Tests/Tests/NativeSegmentationTests.cpp for the sweep.
     *
     * Becomes the default once Essentia goes.
     */
    static Parameters nativeDefaults();

    SBicSegmenter() = default;

    /**
     * @brief Computes segment boundaries using the default parameters.
     * @param audioFile The audio file to analyse.
     * @return Segment boundary timestamps in seconds. Empty on failure.
     */
    std::vector<float> analyze(const juce::File& audioFile);

    /**
     * @brief Computes segment boundaries for the given audio file.
     * @param audioFile The audio file to analyse.
     * @param params Segmentation parameters.
     * @param shouldAbort Optional flag polled between the analysis stages
     *        (decoding, every MFCC frame, the BIC pass). Once set, the
     *        analysis gives up at the next poll and returns empty, so a
     *        caller shutting down (see AnalysisWorker) waits for one stage
     *        at most rather than for the whole file.
     * @return Segment boundary timestamps in seconds. Empty on failure or
     *         when aborted.
     */
    std::vector<float> analyze(const juce::File& audioFile,
                               const Parameters& params,
                               const std::atomic<bool>* shouldAbort = nullptr);

    /**
     * @brief The native implementation, independent of Essentia:
     *        AnalysisAudioLoader -> Stft -> Mfcc -> BicSegmenter.
     *
     * This is what analyze() runs in builds without Essentia, and what it
     * becomes once Essentia goes; until then it is callable on its own so
     * the two can be held against each other. The frame rate is
     * sampleRate / hopSize, so the BIC parameters, given in frames, mean the
     * same at 22050 Hz with a hop of 512 as at 44100 Hz with a hop of 1024.
     */
    std::vector<float> analyzeNative(const juce::File& audioFile,
                                     const Parameters& params,
                                     const std::atomic<bool>* shouldAbort = nullptr);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SBicSegmenter)
};

} // namespace audium
