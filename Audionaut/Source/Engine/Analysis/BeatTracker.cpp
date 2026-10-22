//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "BeatTracker.h"
#include "AnalysisAbort.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace audium {

namespace {

// The tempo a beat sequence follows: the mean gap among the gaps near the
// median, so one dropped or doubled beat does not shift it, and the frame
// quantisation of the individual gaps averages out.
float tempoOfBeats(const std::vector<int>& beats, double framesPerSecond)
{
    if (beats.size() < 2)
        return 0.0f;

    std::vector<int> gaps;
    for (size_t i = 1; i < beats.size(); ++i)
        gaps.push_back(beats[i] - beats[i - 1]);

    auto sorted = gaps;
    std::sort(sorted.begin(), sorted.end());
    const auto median = (double) sorted[sorted.size() / 2];

    double sum = 0.0;
    auto count = 0;
    for (auto gap : gaps)
        if (std::abs((double) gap - median) <= 0.2 * median)
        {
            sum += gap;
            ++count;
        }

    if (count == 0 || sum <= 0.0)
        return 0.0f;
    return (float) (60.0 * framesPerSecond * (double) count / sum);
}

} // namespace

BeatTracker::Result BeatTracker::track(const std::vector<float>& envelope, double framesPerSecond, float tempo) const
{
    return track(envelope, framesPerSecond, tempo, Parameters());
}

BeatTracker::Result BeatTracker::track(const std::vector<float>& envelope,
                                       double framesPerSecond,
                                       float tempo,
                                       const Parameters& params,
                                       const std::atomic<bool>* shouldAbort) const
{
    const auto n = (int) envelope.size();
    if (n == 0 || tempo <= 0.0f || framesPerSecond <= 0.0)
        return {};

    const auto period = std::max(1, (int) std::lround(60.0 * framesPerSecond / (double) tempo));

    // the envelope in units of its standard deviation
    double sum = 0.0, sumOfSquares = 0.0;
    for (auto value : envelope)
    {
        sum += value;
        sumOfSquares += (double) value * (double) value;
    }
    const auto mean = sum / n;
    const auto variance = n > 1 ? std::max(0.0, (sumOfSquares - n * mean * mean) / (n - 1)) : 0.0;
    const auto deviation = std::sqrt(variance);
    std::vector<double> normalised((size_t) n);
    for (int i = 0; i < n; ++i)
        normalised[(size_t) i] = deviation > 0.0 ? envelope[(size_t) i] / deviation : envelope[(size_t) i];

    // local score: the envelope smoothed by a Gaussian a period wide
    std::vector<double> window((size_t) period * 2 + 1);
    for (int k = -period; k <= period; ++k)
    {
        const auto x = (double) k * 32.0 / (double) period;
        window[(size_t) (k + period)] = std::exp(-0.5 * x * x);
    }
    std::vector<double> localScore((size_t) n, 0.0);
    for (int i = 0; i < n; ++i)
    {
        double value = 0.0;
        for (int k = -period; k <= period; ++k)
        {
            const auto j = i - k;
            if (j >= 0 && j < n)
                value += window[(size_t) (k + period)] * normalised[(size_t) j];
        }
        localScore[(size_t) i] = value;
    }
    const auto maxLocalScore = *std::max_element(localScore.begin(), localScore.end());

    // candidate predecessors: half a period to two periods back, with a
    // penalty for departing from the period
    const auto nearest = -(int) std::lround(period / 2.0);
    const auto farthest = -2 * period;
    std::vector<double> transition;
    for (int offset = farthest; offset <= nearest; ++offset)
    {
        const auto ratio = std::log((double) -offset / (double) period);
        transition.push_back(-(double) params.tightness * ratio * ratio);
    }

    std::vector<double> cumulativeScore((size_t) n, 0.0);
    std::vector<int> backlink((size_t) n, -1);
    auto firstBeat = true;

    for (int i = 0; i < n; ++i)
    {
        if ((i & 1023) == 0 && isAborted(shouldAbort))
            return {};

        auto best = -std::numeric_limits<double>::infinity();
        auto bestPredecessor = -1;
        for (size_t j = 0; j < transition.size(); ++j)
        {
            const auto predecessor = i + farthest + (int) j;
            auto candidate = transition[j];
            if (predecessor >= 0)
                candidate += cumulativeScore[(size_t) predecessor];
            if (candidate > best)
            {
                best = candidate;
                bestPredecessor = predecessor;
            }
        }

        cumulativeScore[(size_t) i] = localScore[(size_t) i] + best;

        // a beat chain only starts once there is some onset strength
        if (firstBeat && localScore[(size_t) i] < 0.01 * maxLocalScore)
            backlink[(size_t) i] = -1;
        else
        {
            backlink[(size_t) i] = bestPredecessor;
            firstBeat = false;
        }
    }

    // the last beat: the last local maximum of the cumulative score that
    // reaches half the median of all its local maxima
    std::vector<int> maxima;
    for (int i = 0; i < n; ++i)
    {
        const auto left = i > 0 ? cumulativeScore[(size_t) i - 1] : -std::numeric_limits<double>::infinity();
        const auto right = i + 1 < n ? cumulativeScore[(size_t) i + 1] : -std::numeric_limits<double>::infinity();
        if (cumulativeScore[(size_t) i] > left && cumulativeScore[(size_t) i] >= right)
            maxima.push_back(i);
    }
    if (maxima.empty())
        return {};

    std::vector<double> maximaScores;
    for (auto i : maxima)
        maximaScores.push_back(cumulativeScore[(size_t) i]);
    std::sort(maximaScores.begin(), maximaScores.end());
    const auto median = maximaScores[maximaScores.size() / 2];

    auto last = -1;
    for (auto i : maxima)
        if (cumulativeScore[(size_t) i] * 2.0 > median)
            last = i;
    if (last < 0)
        return {};

    // read the chain back
    std::vector<int> beats;
    for (auto i = last; i >= 0; i = backlink[(size_t) i])
        beats.push_back(i);
    std::reverse(beats.begin(), beats.end());

    // trim weak beats at either edge
    if (params.trim && beats.size() > 1)
    {
        std::vector<double> strength(beats.size());
        for (size_t k = 0; k < beats.size(); ++k)
            strength[k] = localScore[(size_t) beats[k]];

        std::vector<double> smoothed(beats.size());
        double sumOfSquaredSmoothed = 0.0;
        for (size_t k = 0; k < beats.size(); ++k)
        {
            smoothed[k] = strength[k]
                        + 0.5 * (k > 0 ? strength[k - 1] : 0.0)
                        + 0.5 * (k + 1 < beats.size() ? strength[k + 1] : 0.0);
            sumOfSquaredSmoothed += smoothed[k] * smoothed[k];
        }
        const auto threshold = 0.5 * std::sqrt(sumOfSquaredSmoothed / (double) beats.size());

        size_t first = 0;
        while (first < beats.size() && smoothed[first] <= threshold)
            ++first;
        size_t lastValid = beats.size();
        while (lastValid > first && smoothed[lastValid - 1] <= threshold)
            --lastValid;

        beats = std::vector<int>(beats.begin() + (long) first, beats.begin() + (long) lastValid);
    }

    Result result;
    result.beatFrames = std::move(beats);
    result.bpm = tempoOfBeats(result.beatFrames, framesPerSecond);
    return result;
}

} // namespace audium
