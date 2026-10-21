//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "OnsetStrength.h"

#include <algorithm>
#include <cmath>

namespace audium {

OnsetStrength::OnsetStrength(const Parameters& params, int numBins, double sampleRate)
    : parameters(params),
      filterbank(params.numBands, numBins, sampleRate),
      bands((size_t) params.numBands),
      previousDb((size_t) params.numBands),
      currentDb((size_t) params.numBands)
{
}

float OnsetStrength::next(const float* power)
{
    filterbank.apply(power, bands.data());

    for (size_t band = 0; band < bands.size(); ++band)
        currentDb[band] = 10.0f * std::log10(std::max(bands[band], parameters.floor));

    float strength = 0.0f;
    if (hasPrevious)
    {
        for (size_t band = 0; band < bands.size(); ++band)
            strength += std::max(0.0f, currentDb[band] - previousDb[band]);
        strength /= (float) bands.size();
    }

    std::swap(previousDb, currentDb);
    hasPrevious = true;
    return strength;
}

void OnsetStrength::reset()
{
    hasPrevious = false;
}

std::vector<int> OnsetStrength::pickPeaks(const std::vector<float>& envelope, double framesPerSecond)
{
    return pickPeaks(envelope, framesPerSecond, PeakParameters());
}

std::vector<int> OnsetStrength::pickPeaks(const std::vector<float>& envelope,
                                          double framesPerSecond,
                                          const PeakParameters& params)
{
    const auto n = (int) envelope.size();
    if (n == 0)
        return {};

    const auto frames = [framesPerSecond](double seconds) { return (int) (seconds * framesPerSecond); };
    const auto preMax = frames(params.preMaxSeconds);
    const auto postMax = frames(params.postMaxSeconds) + 1;
    const auto preAverage = frames(params.preAverageSeconds);
    const auto postAverage = frames(params.postAverageSeconds) + 1;
    const auto wait = frames(params.waitSeconds);

    // scaled to 0..1
    const auto [lowest, highest] = std::minmax_element(envelope.begin(), envelope.end());
    const auto range = *highest - *lowest;
    if (range <= 0.0f)
        return {};

    std::vector<float> scaled((size_t) n);
    for (int i = 0; i < n; ++i)
        scaled[(size_t) i] = (envelope[(size_t) i] - *lowest) / range;

    std::vector<int> peaks;
    auto lastPeak = -wait - 1;

    for (int i = 0; i < n; ++i)
    {
        if (i - lastPeak <= wait)
            continue;

        const auto value = scaled[(size_t) i];

        auto isMaximum = true;
        for (int k = std::max(0, i - preMax); k < std::min(n, i + postMax) && isMaximum; ++k)
            isMaximum = scaled[(size_t) k] <= value;
        if (! isMaximum)
            continue;

        const auto from = std::max(0, i - preAverage);
        const auto to = std::min(n, i + postAverage);
        float sum = 0.0f;
        for (int k = from; k < to; ++k)
            sum += scaled[(size_t) k];
        const auto mean = sum / (float) (to - from);

        if (value >= mean + params.delta)
        {
            peaks.push_back(i);
            lastPeak = i;
        }
    }

    return peaks;
}

} // namespace audium
