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
 * @class BeatTracker
 * @brief Places beats on an onset strength envelope at a given tempo.
 *
 * Ellis' dynamic programming (2007), as librosa runs it: every frame scores
 * as a beat by the onset strength around it plus the best score of a
 * predecessor about one period earlier, with a penalty growing as the gap
 * departs from the period. The beats are read back from the best-scoring
 * frame near the end, and weak ones at either edge are trimmed.
 */
class BeatTracker {

public:
    struct Parameters {
        /// How strongly the gaps between beats are held to the period.
        float tightness = 100.0f;
        /// Drop beats at the edges whose onset strength is weak.
        bool trim = true;
    };

    struct Result {
        std::vector<int> beatFrames;
        /// The tempo the beats actually follow, from their spacing; 0 when
        /// fewer than two beats were found.
        float bpm = 0.0f;
    };

    BeatTracker() = default;

    /**
     * @brief Tracks the beats.
     * @param envelope The onset strength envelope.
     * @param framesPerSecond Its frame rate.
     * @param tempo The tempo to track at, in BPM (see TempoEstimator).
     * @param params Tracking parameters.
     * @param shouldAbort Optional flag polled every so many frames. Once set,
     *        the tracking gives up and returns empty.
     * @return The beat frames, ascending, and their tempo. Empty when the
     *         envelope or the tempo is empty or the run was abandoned.
     */
    Result track(const std::vector<float>& envelope,
                 double framesPerSecond,
                 float tempo,
                 const Parameters& params,
                 const std::atomic<bool>* shouldAbort = nullptr) const;

    Result track(const std::vector<float>& envelope, double framesPerSecond, float tempo) const;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeatTracker)
};

} // namespace audium
