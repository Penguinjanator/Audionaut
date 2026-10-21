//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "TempoEstimator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace audium {

float TempoEstimator::estimate(const std::vector<float>& envelope, double framesPerSecond) const
{
    return estimate(envelope, framesPerSecond, Parameters());
}

float TempoEstimator::estimate(const std::vector<float>& envelope,
                               double framesPerSecond,
                               const Parameters& params) const
{
    const auto n = (int) envelope.size();
    if (n < 2 || framesPerSecond <= 0.0 || params.minTempo <= 0.0f || params.maxTempo <= params.minTempo)
        return 0.0f;

    const auto maxLag = std::min(params.maxLag, n - 1);
    if (maxLag < 1)
        return 0.0f;

    // autocorrelation, scaled by the energy
    std::vector<double> autocorrelation((size_t) maxLag + 1, 0.0);
    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        for (int t = 0; t + lag < n; ++t)
            sum += (double) envelope[(size_t) t] * (double) envelope[(size_t) (t + lag)];
        autocorrelation[(size_t) lag] = sum;
    }
    if (autocorrelation[0] <= 0.0)
        return 0.0f;

    // A period that is not a whole number of frames splits its correlation
    // between two neighbouring lags, while its double may land on one; a
    // little smoothing across lags keeps that from reading as half the tempo.
    std::vector<double> smoothed(autocorrelation.size());
    for (size_t lag = 0; lag < autocorrelation.size(); ++lag)
    {
        const auto left = lag > 0 ? autocorrelation[lag - 1] : autocorrelation[lag];
        const auto right = lag + 1 < autocorrelation.size() ? autocorrelation[lag + 1] : autocorrelation[lag];
        smoothed[lag] = (0.25 * left + 0.5 * autocorrelation[lag] + 0.25 * right) / autocorrelation[0];
    }
    autocorrelation.swap(smoothed);

    // the lag whose correlation, weighted by the prior, is strongest
    const auto logStart = std::log2((double) params.startTempo);
    auto best = -std::numeric_limits<double>::infinity();
    auto bestLag = 0;
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        const auto bpm = 60.0 * framesPerSecond / (double) lag;
        if (bpm < (double) params.minTempo || bpm > (double) params.maxTempo)
            continue;

        const auto deviation = (std::log2(bpm) - logStart) / (double) params.tempoSpreadOctaves;
        const auto score = -0.5 * deviation * deviation + std::log1p(1.0e6 * autocorrelation[(size_t) lag]);
        if (score > best)
        {
            best = score;
            bestLag = lag;
        }
    }
    if (bestLag == 0)
        return 0.0f;

    // the peak's position between lags, by the parabola through its neighbours
    auto lag = (double) bestLag;
    if (bestLag > 0 && bestLag < maxLag)
    {
        const auto left = autocorrelation[(size_t) bestLag - 1];
        const auto centre = autocorrelation[(size_t) bestLag];
        const auto right = autocorrelation[(size_t) bestLag + 1];
        const auto curvature = left - 2.0 * centre + right;
        if (curvature < 0.0)
            lag += std::clamp(0.5 * (left - right) / curvature, -0.5, 0.5);
    }

    return (float) (60.0 * framesPerSecond / lag);
}

} // namespace audium
