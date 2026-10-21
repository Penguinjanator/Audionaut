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
 * @brief Tracks beat positions in an audio file using Essentia's
 *        BeatTrackerMultiFeature algorithm (streaming mode).
 *
 * As with SBicSegmenter and OnsetSegmenter, the public interface is free of
 * any Essentia types so this header can be included from engine code (e.g.
 * AnalysisProvider) that does not itself link against Essentia. The streaming
 * network (MonoLoader -> BeatTrackerMultiFeature) lives entirely in the .cpp.
 */
class BeatSegmenter {

public:
    /**
     * @brief Beat-tracking method, passed through to RhythmExtractor2013's
     *        "method" parameter.
     */
    enum class Method {
        MultiFeature,
        Degara
    };

    /**
     * @brief Tunable parameters for the beat tracking.
     */
    struct Parameters {
        float sampleRate = 44100.0f;
        Method method = Method::MultiFeature;
        // RhythmExtractor2013's own tempo search range defaults.
        int maxTempo = 208;
        int minTempo = 40;
    };

    /**
     * @brief Result of a beat-tracking pass.
     */
    struct Result {
        std::vector<float> beats;  ///< Beat timestamps in seconds.
        float bpm = 0.0f;          ///< RhythmExtractor2013's overall BPM estimate.
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
     * @param shouldAbort Optional flag polled between the streaming network's
     *        steps. Once set, the analysis gives up at the next poll and
     *        returns empty, so a caller shutting down (see AnalysisWorker)
     *        waits for one step at most rather than for the whole file.
     * @return Beat timestamps and BPM estimate. Empty/zero on failure or when
     *         aborted.
     */
    Result analyze(const juce::File& audioFile,
                   const Parameters& params,
                   const std::atomic<bool>* shouldAbort = nullptr);

    /**
     * @brief The parameters the native implementation is calibrated for:
     *        22050 Hz, analysed in 2048-sample frames with a hop of 512. The
     *        method is ignored - there is one native tracker. Becomes the
     *        default once Essentia goes.
     */
    static Parameters nativeDefaults();

    /**
     * @brief The native implementation, independent of Essentia:
     *        AnalysisAudioLoader -> Stft -> OnsetStrength -> TempoEstimator
     *        -> BeatTracker.
     *
     * What analyze() runs in builds without Essentia, and what it becomes
     * once Essentia goes; until then callable on its own so the two can be
     * held against each other. The BPM is the tempo the tracked beats
     * follow, which is what the grid snapping and the clip tempo consume.
     */
    Result analyzeNative(const juce::File& audioFile,
                         const Parameters& params,
                         const std::atomic<bool>* shouldAbort = nullptr);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeatSegmenter)
};

} // namespace audium
