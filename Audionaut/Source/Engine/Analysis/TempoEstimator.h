//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once

#include <vector>
#include <JuceHeader.h>

namespace audium {

/**
 * @class TempoEstimator
 * @brief The dominant tempo of an onset strength envelope.
 *
 * The envelope's autocorrelation peaks at the beat period and its multiples;
 * a log-normal prior centred on a typical tempo settles which of them is the
 * beat (Ellis, 2007, as librosa's tempo estimator applies it). The peak's
 * position is refined between lags, so the estimate is not quantised to the
 * frame rate.
 */
class TempoEstimator {

public:
    struct Parameters {
        float minTempo = 40.0f;
        float maxTempo = 208.0f;
        /// Centre of the prior, in BPM. The autocorrelation of real material
        /// peaks about as strongly at the beat as at its double or half, so
        /// this is what decides between them. 110 sits where the test
        /// recordings (87 to 131 BPM) all come out at the level Essentia
        /// reported them; 120 tips the slowest of them to its double. See
        /// the tempo sweep in Catch2Tests/Tests/NativeRhythmTests.cpp.
        float startTempo = 110.0f;
        /// Width of the prior, in octaves.
        float tempoSpreadOctaves = 1.0f;
        /// Longest lag considered, in frames.
        int maxLag = 384;
    };

    TempoEstimator() = default;

    /**
     * @brief Estimates the tempo.
     * @param envelope The onset strength envelope.
     * @param framesPerSecond Its frame rate.
     * @param params Estimation parameters.
     * @return The tempo in BPM, or 0 when there is nothing to estimate from.
     */
    float estimate(const std::vector<float>& envelope,
                   double framesPerSecond,
                   const Parameters& params) const;

    float estimate(const std::vector<float>& envelope, double framesPerSecond) const;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TempoEstimator)
};

} // namespace audium
