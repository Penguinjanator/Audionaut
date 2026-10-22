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
 * @class BicSegmenter
 * @brief Finds the frames at which a feature sequence changes character,
 *        by the Bayesian Information Criterion.
 *
 * Each candidate split of a window is scored by how much better two Gaussians
 * (full covariance, one per side) explain the frames than one does, less a
 * penalty for the extra parameters (Chen & Gopalakrishnan, 1998). The window
 * grows from the last boundary until a split scores positive: a coarse pass
 * over the window picks the best candidate, a finer pass around it settles
 * the frame (Tritschler & Gopinath, 1999). A final pass re-scores every
 * boundary against its two neighbouring segments and drops the ones that no
 * longer hold up.
 *
 * Frame-based throughout; what a frame is in seconds is the caller's affair.
 */
class BicSegmenter {

public:
    struct Parameters {
        int size1 = 200;   ///< Initial window after a boundary, in frames.
        int inc1 = 60;     ///< Coarse candidate step, and the window's growth per round.
        int size2 = 300;   ///< Fine pass span around the coarse candidate.
        int inc2 = 20;     ///< Fine candidate step.
        /// Margin from a window's edges within which no split is scored, and
        /// half the shortest segment allowed.
        int minLength = 50;
        /// Weight of the penalty term. Lower finds more boundaries.
        float complexityPenaltyWeight = 0.5f;
        /// Corrects each log-determinant for the frames it was estimated
        /// from, so short stretches are not favoured. Off only for study.
        bool correctSmallSampleBias = true;
        /// The window stops growing here and slides instead, bounding the work
        /// on long stretches where nothing changes.
        int maxWindow = 800;
    };

    BicSegmenter() = default;

    /**
     * @brief Segments the sequence.
     * @param features One vector of coefficients per frame, all the same size.
     * @param params Segmentation parameters.
     * @param shouldAbort Optional flag polled once per window round. Once set,
     *        the segmentation gives up and returns empty.
     * @return Boundary frame indices, ascending, starting with 0 and ending
     *         with the number of frames. Empty when there are no frames or the
     *         run was abandoned.
     */
    std::vector<int> segment(const std::vector<std::vector<float>>& features,
                             const Parameters& params,
                             const std::atomic<bool>* shouldAbort = nullptr) const;

    /// Segments with the default parameters.
    std::vector<int> segment(const std::vector<std::vector<float>>& features) const;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BicSegmenter)
};

} // namespace audium
