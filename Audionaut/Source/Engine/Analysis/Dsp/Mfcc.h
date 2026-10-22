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
 * @class Mfcc
 * @brief Mel-frequency cepstral coefficients of a power spectrum.
 *
 * Mel band energies (see MelFilterbank), their natural logarithm, and an
 * orthonormal DCT-II over the bands (Davis & Mermelstein, 1980), keeping the
 * first numCoefficients - the zeroth, a measure of overall level, included.
 */
class Mfcc {

public:
    struct Parameters {
        int numBands = 40;
        int numCoefficients = 13;
        double lowFrequency = 0.0;
        double highFrequency = 0.0;   ///< 0 means Nyquist.
        /// Band energies below this are clamped before the log. With the
        /// signal peak-normalised, this is roughly -100 dB.
        float floor = 1.0e-10f;
    };

    /**
     * @param params Feature parameters.
     * @param numBins Bins per input spectrum, DC to Nyquist.
     * @param sampleRate Sample rate of the analysed signal.
     */
    Mfcc(const Parameters& params, int numBins, double sampleRate);

    int getNumCoefficients() const { return parameters.numCoefficients; }
    int getNumBands() const { return parameters.numBands; }

    /**
     * @brief Computes the coefficients of one spectrum.
     * @param power A spectrum of numBins values.
     * @param coefficients Receives getNumCoefficients() values.
     */
    void compute(const float* power, float* coefficients);

    /**
     * @brief The log and DCT stages alone, from band energies.
     * @param bands getNumBands() band energies.
     * @param coefficients Receives getNumCoefficients() values.
     */
    void computeFromBands(const float* bands, float* coefficients) const;

private:
    Parameters parameters;
    MelFilterbank filterbank;
    std::vector<float> dct;   // numCoefficients x numBands
    std::vector<float> bandsScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Mfcc)
};

} // namespace audium
