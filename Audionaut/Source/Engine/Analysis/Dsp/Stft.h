//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once

#include <atomic>
#include <functional>
#include <vector>
#include <JuceHeader.h>

namespace audium {

/**
 * @class Stft
 * @brief Short-time Fourier transform yielding one power spectrum per frame.
 *
 * Frames are centred: frame t covers the hopSize * t sample, with the signal
 * zero-padded by half a frame at both ends, so a frame index converts to time
 * as t * hopSize / sampleRate with no offset. The frames are handed to a
 * callback one at a time; nothing holds a whole spectrogram, so hour-long
 * recordings cost no more memory than short ones.
 */
class Stft {

public:
    struct Parameters {
        int frameSize = 2048;  ///< A power of two. Hann-windowed.
        int hopSize = 512;
    };

    /// The default parameters: 2048-sample frames, hop 512.
    Stft();
    explicit Stft(const Parameters& params);

    const Parameters& getParameters() const { return parameters; }

    /// The number of frames a signal of this length produces: one per hop
    /// whose centre falls inside the signal.
    int numFrames(size_t numSamples) const;

    /// Bins per power spectrum: frameSize / 2 + 1, DC to Nyquist.
    int numBins() const;

    /// Centre frequency of a bin in Hz.
    double binFrequency(int bin, double sampleRate) const;

    /**
     * @brief Runs the transform over the signal.
     * @param signal The mono signal.
     * @param frameFn Called once per frame, in order, with the frame index and
     *        the power spectrum (numBins() values, valid during the call only).
     * @param shouldAbort Optional flag polled every few frames. Once set, the
     *        run stops.
     * @return False when the run was abandoned.
     */
    bool forEachPowerFrame(const std::vector<float>& signal,
                           const std::function<void(int frameIndex, const float* power)>& frameFn,
                           const std::atomic<bool>* shouldAbort = nullptr) const;

private:
    Parameters parameters;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Stft)
};

} // namespace audium
