//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "Mfcc.h"

#include <algorithm>
#include <cmath>

namespace audium {

Mfcc::Mfcc(const Parameters& params, int numBins, double sampleRate)
    : parameters(params),
      filterbank(params.numBands, numBins, sampleRate, params.lowFrequency, params.highFrequency),
      bandsScratch((size_t) params.numBands)
{
    jassert(params.numCoefficients > 0 && params.numCoefficients <= params.numBands);

    // DCT-II, orthonormal: c_n = s_n * sum_m x_m cos(pi * n * (m + 1/2) / M)
    const auto numBands = params.numBands;
    const auto pi = juce::MathConstants<double>::pi;
    dct.resize((size_t) params.numCoefficients * (size_t) numBands);
    for (int n = 0; n < params.numCoefficients; ++n)
    {
        const auto scale = std::sqrt((n == 0 ? 1.0 : 2.0) / (double) numBands);
        for (int m = 0; m < numBands; ++m)
            dct[(size_t) n * (size_t) numBands + (size_t) m]
                = (float) (scale * std::cos(pi * (double) n * ((double) m + 0.5) / (double) numBands));
    }
}

void Mfcc::compute(const float* power, float* coefficients)
{
    filterbank.apply(power, bandsScratch.data());
    computeFromBands(bandsScratch.data(), coefficients);
}

void Mfcc::computeFromBands(const float* bands, float* coefficients) const
{
    const auto numBands = parameters.numBands;
    for (int n = 0; n < parameters.numCoefficients; ++n)
    {
        const auto* row = dct.data() + (size_t) n * (size_t) numBands;
        float sum = 0.0f;
        for (int m = 0; m < numBands; ++m)
            sum += row[m] * std::log(std::max(bands[m], parameters.floor));
        coefficients[n] = sum;
    }
}

} // namespace audium
