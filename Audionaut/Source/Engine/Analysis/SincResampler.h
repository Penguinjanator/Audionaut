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
 * @class SincResampler
 * @brief Changes a signal's sample rate with a windowed sinc whose cutoff
 *        follows the lower of the two rates.
 *
 * Each output sample is the input convolved with a Kaiser-windowed sinc
 * centred on its position, the sinc's cutoff at the output Nyquist when
 * the rate goes down and at the input Nyquist when it goes up, so nothing
 * above the lower Nyquist aliases into the result. This is what
 * libsamplerate does, and what Essentia's loader did; JUCE's own
 * ResamplingAudioSource is a second-order IIR at the new Nyquist, which
 * leaves the top of the band bent and lets the octave above it through.
 *
 * The kernel is tabulated once per ratio as polyphase rows, one per
 * fractional position, so an output sample is one dot product: at 2:1 with
 * the default width, 129 multiplies.
 */
class SincResampler {

public:
    struct Parameters {
        /// Half the kernel's width, in samples of the lower rate. With the
        /// window below, 32 puts the transition between 0.85 of the lower
        /// Nyquist and the Nyquist itself: flat to 0.1 dB up to 0.85, better
        /// than 70 dB down from 1.0. The top mel band lives in the
        /// transition either way, as it did with Essentia's resampler.
        int halfWidth = 32;
        /// Kaiser window shape; 8 is an 80 dB window.
        double kaiserBeta = 8.0;
        /// Centre of the transition, as a fraction of the lower Nyquist.
        double cutoff = 0.925;
    };

    /**
     * @param inputRate The signal's sample rate.
     * @param outputRate The rate wanted.
     * @param params Kernel parameters.
     */
    SincResampler(double inputRate, double outputRate, const Parameters& params);
    SincResampler(double inputRate, double outputRate);

    /// Output samples a signal of this length produces.
    size_t outputLength(size_t inputLength) const;

    /**
     * @brief Resamples the whole signal.
     * @param input The signal at the input rate.
     * @param shouldAbort Optional flag polled every few thousand output
     *        samples; once set, the result is empty.
     * @return The signal at the output rate, outputLength(input.size())
     *         samples long. The input is treated as zero outside its ends.
     */
    std::vector<float> process(const std::vector<float>& input,
                               const std::atomic<bool>* shouldAbort = nullptr) const;

private:
    double inputRate = 0.0, outputRate = 0.0;
    double ratio = 1.0;          ///< input samples per output sample
    int halfWidthIn = 0;         ///< kernel half-width in input samples
    int numTaps = 0;             ///< 2 * halfWidthIn + 1
    int numPhases = 1024;        ///< fractional positions tabulated
    std::vector<float> rows;     ///< numPhases rows of numTaps

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SincResampler)
};

} // namespace audium
