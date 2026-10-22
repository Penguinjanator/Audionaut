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
 * @class BeatSegmenter
 * @brief Tracks the beats of an audio file and reports their tempo.
 *
 * The chain is AnalysisAudioLoader -> Stft -> OnsetStrength -> TempoEstimator
 * -> BeatTracker, run by AnalysisRun, which shares the front end with the
 * other analyses. This class is the single-type door to it: a file in, beat
 * timestamps plus a BPM out. The BPM is the tempo the tracked beats actually
 * follow, which is what the grid snapping and the clip tempo consume.
 */
class BeatSegmenter {

public:
    /**
     * @brief Tunable parameters for the beat tracking.
     */
    struct Parameters {
        /// The audio is resampled to this rate and analysed in 2048-sample
        /// frames with a hop of 512.
        float sampleRate = 22050.0f;
        /// The tempo search range, in BPM.
        int maxTempo = 208;
        int minTempo = 40;
        /// Track the beats on the flux of the band magnitudes rather than
        /// of their dB values: see OnsetStrength::Flux. The tempo is always
        /// estimated on the dB envelope.
        bool linearBeatFlux = true;
    };

    /**
     * @brief Result of a beat-tracking pass.
     */
    struct Result {
        std::vector<float> beats;  ///< Beat timestamps in seconds.
        float bpm = 0.0f;          ///< The tempo of the beats.
    };

    BeatSegmenter() = default;

    /**
     * @brief Tracks beats using the default parameters.
     * @param audioFile The audio file to analyse.
     * @return Beat timestamps and BPM estimate. Empty/zero on failure.
     */
    Result analyze(const juce::File& audioFile);

    /**
     * @brief Tracks beats for the given audio file.
     * @param audioFile The audio file to analyse.
     * @param params Beat-tracking parameters.
     * @param shouldAbort Optional flag polled between the analysis stages
     *        (decoding, the STFT frames, the tracking). Once set, the
     *        analysis gives up at the next poll and returns empty, so a
     *        caller shutting down (see AnalysisWorker) waits for one stage
     *        at most rather than for the whole file.
     * @return Beat timestamps and BPM estimate. Empty/zero on failure or when
     *         aborted.
     */
    Result analyze(const juce::File& audioFile,
                   const Parameters& params,
                   const std::atomic<bool>* shouldAbort = nullptr);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeatSegmenter)
};

} // namespace audium
