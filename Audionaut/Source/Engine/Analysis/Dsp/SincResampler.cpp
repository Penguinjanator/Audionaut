//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "SincResampler.h"
#include "../AnalysisAbort.h"

#include <algorithm>
#include <cmath>

namespace audium {

namespace {

// Modified Bessel function of the first kind, order zero, by its series.
double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    const auto half = x / 2.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (half / k) * (half / k);
        sum += term;
        if (term < sum * 1.0e-12)
            break;
    }
    return sum;
}

} // namespace

SincResampler::SincResampler(double inputRate, double outputRate)
    : SincResampler(inputRate, outputRate, Parameters())
{
}

SincResampler::SincResampler(double inputRateToUse, double outputRateToUse, const Parameters& params)
    : inputRate(inputRateToUse), outputRate(outputRateToUse)
{
    jassert(inputRate > 0.0 && outputRate > 0.0 && params.halfWidth > 0);
    ratio = inputRate / outputRate;

    // The kernel is a sinc at the lower Nyquist, in input-sample units: a
    // cutoff of fc cycles per input sample means sinc(2 fc k). Going down,
    // fc = cutoff / (2 ratio); going up the input band is already narrower
    // than the output's, so fc = cutoff / 2 and the kernel is a plain
    // interpolator.
    const auto stretch = std::max(1.0, ratio);
    const auto fc = params.cutoff / (2.0 * stretch);
    halfWidthIn = (int) std::ceil(params.halfWidth * stretch);
    numTaps = 2 * halfWidthIn + 1;

    const auto windowed = [&](double x)
    {
        const auto t = x / (double) (halfWidthIn + 1);
        if (std::abs(t) >= 1.0)
            return 0.0;
        const auto window = besselI0(params.kaiserBeta * std::sqrt(1.0 - t * t)) / besselI0(params.kaiserBeta);
        const auto arg = 2.0 * juce::MathConstants<double>::pi * fc * x;
        const auto sinc = x == 0.0 ? 1.0 : std::sin(arg) / arg;
        // 2 fc is the kernel's DC gain for a sum over integer positions
        return 2.0 * fc * sinc * window;
    };

    // One row per fractional phase: row p holds the kernel sampled at the
    // integer offsets minus p / numPhases, so an output sample is one dot
    // product of a row with the input around its position.
    rows.resize((size_t) numPhases * (size_t) numTaps);
    for (int phase = 0; phase < numPhases; ++phase)
    {
        const auto frac = (double) phase / (double) numPhases;
        auto* row = rows.data() + (size_t) phase * (size_t) numTaps;
        for (int j = 0; j < numTaps; ++j)
            row[j] = (float) windowed((double) (j - halfWidthIn) - frac);
    }
}

size_t SincResampler::outputLength(size_t inputLength) const
{
    // from the rates, not the ratio, so 48000 at 22050 gives 22050 and not
    // one sample short of it
    return inputLength == 0 ? 0 : (size_t) std::max(1.0, std::floor((double) inputLength * outputRate / inputRate + 1.0e-6));
}

std::vector<float> SincResampler::process(const std::vector<float>& input,
                                          const std::atomic<bool>* shouldAbort,
                                          const std::function<void(double)>& progress) const
{
    const auto n = (long) input.size();
    std::vector<float> output(outputLength(input.size()));
    const auto* in = input.data();

    for (size_t m = 0; m < output.size(); ++m)
    {
        if ((m & 4095) == 0)
        {
            if (isAborted(shouldAbort))
                return {};
            if (progress)
                progress((double) m / (double) output.size());
        }

        // the output sample's position in input samples, split into the
        // integer part and the nearest tabulated phase
        const auto position = (double) m * ratio;
        auto centre = (long) std::floor(position);
        auto phase = (int) std::lround((position - (double) centre) * numPhases);
        if (phase >= numPhases)
        {
            phase -= numPhases;
            ++centre;
        }
        const auto* row = rows.data() + (size_t) phase * (size_t) numTaps;

        const auto first = centre - halfWidthIn;
        const auto last = centre + halfWidthIn;
        float sum = 0.0f;
        if (first >= 0 && last < n)
        {
            const auto* x = in + first;
            for (int j = 0; j < numTaps; ++j)
                sum += x[j] * row[j];
        }
        else
        {
            // the ends: the input is zero outside itself
            for (auto k = std::max(0L, first); k <= std::min(n - 1, last); ++k)
                sum += in[k] * row[k - first];
        }
        output[m] = sum;
    }

    return output;
}

} // namespace audium
