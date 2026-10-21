//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once

#include <atomic>
#include <optional>
#include <vector>
#include <JuceHeader.h>

namespace audium {

/**
 * @brief A whole audio file as one mono signal, as the analyses want it.
 */
struct MonoAudio {
    std::vector<float> samples;
    double sampleRate = 0.0;
};

/**
 * @class AnalysisAudioLoader
 * @brief Decodes an audio file into a mono signal at the analysis rate.
 *
 * All analyses start the same way: the channels are averaged into one, the
 * result is resampled to the analysis rate, the DC offset is removed and the
 * peak is brought to 1 so quiet recordings are analysed like loud ones. The
 * features downstream are gain-invariant apart from their log floors, which
 * the normalisation makes amplitude-relative.
 *
 * Decoding streams through the file in blocks, so the only buffer held is the
 * mono result (a little over 5 MB per minute at 22050 Hz).
 */
class AnalysisAudioLoader {

public:
    struct Parameters {
        /// Where the signal ends up. 22050 Hz with a hop of 512 samples gives
        /// the 43.07 frames per second the analysis parameters are tuned for.
        double targetSampleRate = 22050.0;
        bool removeDcOffset = true;
        bool normalisePeak = true;
    };

    AnalysisAudioLoader() = default;

    /**
     * @brief Loads the file.
     * @param audioFile The file to decode. Anything the app's formats read.
     * @param params Loading parameters.
     * @param shouldAbort Optional flag polled once per decoded block. Once
     *        set, loading gives up and returns nothing.
     * @return The mono signal, or nothing when the file is missing, cannot
     *         be read, holds no samples, or the load was abandoned.
     */
    std::optional<MonoAudio> load(const juce::File& audioFile,
                                  const Parameters& params,
                                  const std::atomic<bool>* shouldAbort = nullptr) const;

    /// Loads with the default parameters.
    std::optional<MonoAudio> load(const juce::File& audioFile) const;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisAudioLoader)
};

} // namespace audium
