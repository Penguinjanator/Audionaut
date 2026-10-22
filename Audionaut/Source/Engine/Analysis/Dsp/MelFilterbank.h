//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once

#include <vector>
#include <JuceHeader.h>

namespace audium {

/**
 * @class MelFilterbank
 * @brief Triangular filters on the mel scale, applied to power spectra.
 *
 * The scale and normalisation are Slaney's (Auditory Toolbox, 1998), as
 * librosa uses them by default: linear below 1 kHz, logarithmic above, each
 * filter scaled to unit area so a flat spectrum gives equal band energies.
 */
class MelFilterbank {

public:
    /**
     * @param numBands Number of filters.
     * @param numBins Bins per input spectrum, DC to Nyquist.
     * @param sampleRate Sample rate of the analysed signal.
     * @param lowFrequency Lower edge of the first filter in Hz.
     * @param highFrequency Upper edge of the last filter in Hz; 0 means Nyquist.
     */
    MelFilterbank(int numBands, int numBins, double sampleRate,
                  double lowFrequency = 0.0, double highFrequency = 0.0);

    int getNumBands() const { return (int) filters.size(); }
    int getNumBins() const { return numBins; }

    /// Centre frequency of a band in Hz.
    double bandFrequency(int band) const { return centres[(size_t) band]; }

    /**
     * @brief Applies the filters.
     * @param power A spectrum of getNumBins() values.
     * @param bands Receives getNumBands() band energies.
     */
    void apply(const float* power, float* bands) const;

    static double hertzToMel(double hertz);
    static double melToHertz(double mel);

private:
    struct Filter {
        int firstBin = 0;
        std::vector<float> weights;
    };

    int numBins = 0;
    std::vector<Filter> filters;
    std::vector<double> centres;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelFilterbank)
};

} // namespace audium
