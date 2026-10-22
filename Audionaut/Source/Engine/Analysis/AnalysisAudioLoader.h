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
 * The rate change is a windowed sinc with its cutoff at the lower Nyquist
 * (SincResampler), so nothing above it aliases into the analysis. Decoding
 * streams through the file in blocks into one mono buffer at the file's
 * rate, which the resampled result replaces: at most a few tens of MB for a
 * long recording, held only while loading.
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

    /**
     * @brief Why load() would return nothing for this file, or an empty
     *        string when it can be decoded: missing, no reader for its
     *        format on this platform, or no samples in it. Opens the file
     *        but decodes nothing, so a caller can report the problem up
     *        front instead of handing on an empty analysis as a result.
     */
    static juce::String describeProblem(const juce::File& audioFile);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisAudioLoader)
};

} // namespace audium
