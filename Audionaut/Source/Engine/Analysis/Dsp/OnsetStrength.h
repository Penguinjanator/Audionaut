//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once

#include <vector>
#include <JuceHeader.h>

#include "MelFilterbank.h"

namespace audium {

/**
 * @class OnsetStrength
 * @brief How much louder each frame is than the one before, across a mel
 *        spectrum: the onset strength envelope the beat tracker and the onset
 *        detector both work from.
 *
 * Spectral flux on a log-mel spectrogram (Böck & Widmer, 2013), in the form
 * librosa computes it: the positive dB differences between consecutive
 * frames, averaged over the bands. Fed one spectrum at a time so nothing
 * holds the whole spectrogram.
 */
class OnsetStrength {

public:
    struct Parameters {
        int numBands = 128;
        /// Band energies below this are clamped before the log, so bands
        /// without signal do not add noise to the flux. With the signal
        /// peak-normalised, this sits about 80 dB under a loud band.
        float floor = 1.0e-4f;
    };

    /**
     * @brief The two readings of one frame.
     *
     * The dB flux is librosa's envelope and what the onset detector and the
     * tempo estimator want: every rise counts, however quiet. For the beat
     * tracker it is the wrong envelope. It weighs a hi-hat's rise like a
     * kick's, and since most mel bands lie above 1 kHz a sixteenth-note
     * hi-hat pattern outweighs the kick, so the tracker locks a sixteenth
     * off the beat. The flux of the band magnitudes lets the loud hits
     * dominate, which is also how Essentia's onset function saw the signal.
     */
    struct Flux {
        float db = 0.0f;
        float magnitude = 0.0f;
    };

    /**
     * @param params Envelope parameters.
     * @param numBins Bins per input spectrum, DC to Nyquist.
     * @param sampleRate Sample rate of the analysed signal.
     */
    OnsetStrength(const Parameters& params, int numBins, double sampleRate);

    int getNumBands() const { return parameters.numBands; }

    /**
     * @brief Feeds the next power spectrum.
     * @return The dB flux of this frame; 0 for the first.
     */
    float next(const float* power);

    /**
     * @brief Feeds the next power spectrum and reads both fluxes off the one
     *        set of band energies.
     */
    Flux nextBoth(const float* power);

    /// Forgets the previous frame, for starting over on another signal.
    void reset();

    /**
     * @brief Peak picking, in the terms librosa's onset detector uses: a frame
     *        is an onset when it is the maximum of its surroundings, exceeds
     *        the local mean by a margin, and enough time has passed since the
     *        last one. The envelope is scaled to 0..1 first.
     */
    struct PeakParameters {
        double preMaxSeconds = 0.03;
        double postMaxSeconds = 0.0;
        double preAverageSeconds = 0.10;
        double postAverageSeconds = 0.10;
        double waitSeconds = 0.03;
        float delta = 0.07f;
    };

    static std::vector<int> pickPeaks(const std::vector<float>& envelope,
                                      double framesPerSecond,
                                      const PeakParameters& params);

    static std::vector<int> pickPeaks(const std::vector<float>& envelope, double framesPerSecond);

private:
    Parameters parameters;
    MelFilterbank filterbank;
    std::vector<float> bands, previousDb, currentDb, previousMagnitude, currentMagnitude;
    bool hasPrevious = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OnsetStrength)
};

} // namespace audium
