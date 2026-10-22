//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "BicSegmenter.h"
#include "AnalysisAbort.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace audium {

namespace {

// The digamma function, by recurrence into the range where its asymptotic
// series holds.
double digamma(double x)
{
    double result = 0.0;
    while (x < 6.0)
    {
        result -= 1.0 / x;
        x += 1.0;
    }
    const auto inverse = 1.0 / x;
    const auto inverseSquared = inverse * inverse;
    result += std::log(x) - 0.5 * inverse
            - inverseSquared * (1.0 / 12.0 - inverseSquared * (1.0 / 120.0 - inverseSquared / 252.0));
    return result;
}

// Cumulative sums of the features and their outer products, so the covariance
// of any frame range costs O(d^2) rather than a pass over the frames.
class FeatureStatistics {
public:
    explicit FeatureStatistics(const std::vector<std::vector<float>>& features)
        : numFrames((int) features.size()),
          dimensions(features.empty() ? 0 : (int) features.front().size()),
          numPairs(dimensions * (dimensions + 1) / 2),
          sums((size_t) (numFrames + 1) * (size_t) dimensions, 0.0),
          squares((size_t) (numFrames + 1) * (size_t) numPairs, 0.0),
          covariance((size_t) dimensions * (size_t) dimensions),
          cholesky((size_t) dimensions * (size_t) dimensions)
    {
        for (int t = 0; t < numFrames; ++t)
        {
            const auto& frame = features[(size_t) t];
            jassert((int) frame.size() == dimensions);

            auto* sum = sums.data() + (size_t) (t + 1) * (size_t) dimensions;
            const auto* previousSum = sum - dimensions;
            auto* square = squares.data() + (size_t) (t + 1) * (size_t) numPairs;
            const auto* previousSquare = square - numPairs;

            for (int i = 0; i < dimensions; ++i)
                sum[i] = previousSum[i] + (double) frame[(size_t) i];

            auto pair = 0;
            for (int i = 0; i < dimensions; ++i)
                for (int j = i; j < dimensions; ++j, ++pair)
                    square[pair] = previousSquare[pair] + (double) frame[(size_t) i] * (double) frame[(size_t) j];
        }
    }

    int getNumFrames() const { return numFrames; }
    int getDimensions() const { return dimensions; }

    /// Whether logDetCovariance removes the estimate's expected shortfall;
    /// see smallSampleBias.
    bool correctSmallSampleBias = true;

    // How much better two Gaussians explain [a, b) split at i than one does,
    // less the penalty for their extra parameters. Positive favours a change.
    double deltaBic(int a, int i, int b, double penaltyWeight)
    {
        const auto n = (double) (b - a);
        const auto d = (double) dimensions;
        const auto penalty = 0.5 * (d + 0.5 * d * (d + 1.0)) * std::log(n);

        return 0.5 * (n * logDetCovariance(a, b)
                      - (double) (i - a) * logDetCovariance(a, i)
                      - (double) (b - i) * logDetCovariance(i, b))
             - penaltyWeight * penalty;
    }

private:
    // log |Sigma| of the frames in [a, b), via Cholesky. A small ridge keeps a
    // stretch of constant features from making the matrix singular.
    double logDetCovariance(int a, int b)
    {
        const auto n = (double) (b - a);
        const auto* sumA = sums.data() + (size_t) a * (size_t) dimensions;
        const auto* sumB = sums.data() + (size_t) b * (size_t) dimensions;
        const auto* squareA = squares.data() + (size_t) a * (size_t) numPairs;
        const auto* squareB = squares.data() + (size_t) b * (size_t) numPairs;

        auto pair = 0;
        double trace = 0.0;
        for (int i = 0; i < dimensions; ++i)
        {
            const auto meanI = (sumB[i] - sumA[i]) / n;
            for (int j = i; j < dimensions; ++j, ++pair)
            {
                const auto meanJ = (sumB[j] - sumA[j]) / n;
                const auto index = (size_t) i * (size_t) dimensions + (size_t) j;
                const auto value = (squareB[pair] - squareA[pair]) / n - meanI * meanJ;
                covariance[index] = value;
                covariance[(size_t) j * (size_t) dimensions + (size_t) i] = value;
                if (i == j)
                    trace += value;
            }
        }

        // The log-determinant of a covariance estimated from n frames comes
        // out low by an amount that depends only on n and d (the estimate is
        // Wishart), which makes a short stretch look spuriously distinct.
        // Removing the expected shortfall leaves an unbiased estimate.
        const auto bias = smallSampleBias(n);

        const auto ridge = std::max(1.0e-12, 1.0e-6 * trace / (double) dimensions);
        for (int i = 0; i < dimensions; ++i)
            covariance[(size_t) i * (size_t) dimensions + (size_t) i] += ridge;

        // L L^T = Sigma; log |Sigma| = 2 sum log L_ii
        double logDet = 0.0;
        for (int i = 0; i < dimensions; ++i)
        {
            for (int j = 0; j <= i; ++j)
            {
                auto value = covariance[(size_t) i * (size_t) dimensions + (size_t) j];
                for (int k = 0; k < j; ++k)
                    value -= cholesky[(size_t) i * (size_t) dimensions + (size_t) k]
                           * cholesky[(size_t) j * (size_t) dimensions + (size_t) k];

                if (i == j)
                {
                    // Numerically the matrix can still fail to be positive
                    // definite; treat such a direction as having the ridge's
                    // variance rather than giving up.
                    const auto diagonal = std::sqrt(std::max(value, ridge));
                    cholesky[(size_t) i * (size_t) dimensions + (size_t) i] = diagonal;
                    logDet += 2.0 * std::log(diagonal);
                }
                else
                {
                    cholesky[(size_t) i * (size_t) dimensions + (size_t) j]
                        = value / cholesky[(size_t) j * (size_t) dimensions + (size_t) j];
                }
            }
        }
        return logDet - bias;
    }

    // E[log |S|] - log |Sigma| for a sample covariance S of n frames in d
    // dimensions: sum_i psi((n - i + 1) / 2) + d log 2 - d log n.
    double smallSampleBias(double n)
    {
        if (! correctSmallSampleBias || n <= (double) dimensions)
            return 0.0;

        const auto index = (size_t) n;
        if (index < biasCache.size() && biasCache[index] != 0.0)
            return biasCache[index];

        double bias = (double) dimensions * (std::log(2.0) - std::log(n));
        for (int i = 1; i <= dimensions; ++i)
            bias += digamma((n - (double) i + 1.0) / 2.0);

        if (index < biasCache.size())
            biasCache[index] = bias;
        return bias;
    }

    int numFrames, dimensions, numPairs;
    std::vector<double> sums, squares;
    std::vector<double> covariance, cholesky;
    std::vector<double> biasCache = std::vector<double>((size_t) numFrames + 1, 0.0);
};

} // namespace

std::vector<int> BicSegmenter::segment(const std::vector<std::vector<float>>& features) const
{
    return segment(features, Parameters());
}

std::vector<int> BicSegmenter::segment(const std::vector<std::vector<float>>& features,
                                       const Parameters& params,
                                       const std::atomic<bool>* shouldAbort) const
{
    if (features.empty() || features.front().empty())
        return {};

    jassert(params.inc1 > 0 && params.inc2 > 0 && params.minLength > 0);

    FeatureStatistics statistics(features);
    statistics.correctSmallSampleBias = params.correctSmallSampleBias;
    const auto numFrames = statistics.getNumFrames();
    const auto penaltyWeight = (double) params.complexityPenaltyWeight;
    const auto minLength = std::max(1, params.minLength);

    // No candidate closer than minLength to either edge of the window it is
    // scored in, so both sides have frames to estimate from; and no segment
    // shorter than twice that. The stretch right after a cut is often
    // transitional - a decay, a fill - and with a segment allowed to be as
    // short as the margin, a second cut tends to land one margin after the
    // first, on nothing the listener would call a change.
    const auto minSegment = 2 * minLength;

    // The best split of [a, b) among candidates from..to in steps; the score
    // and the frame, or no frame when there are no candidates.
    const auto bestSplit = [&](int a, int b, int from, int to, int step, int& bestFrame)
    {
        auto best = -std::numeric_limits<double>::infinity();
        bestFrame = -1;
        for (int i = from; i <= to; i += step)
        {
            const auto score = statistics.deltaBic(a, i, b, penaltyWeight);
            if (score > best)
            {
                best = score;
                bestFrame = i;
            }
        }
        return best;
    };

    std::vector<int> boundaries{0};
    auto a = 0;
    auto b = std::min(numFrames, params.size1);

    while (true)
    {
        if (isAborted(shouldAbort))
            return {};

        // a is the last boundary, or where the window was slid to; either way
        // the last boundary is no later than a.
        const auto earliest = boundaries.back() + minSegment;

        int coarseFrame;
        const auto coarse = bestSplit(a, b, std::max(a + minLength, earliest), b - minLength, params.inc1, coarseFrame);

        if (coarseFrame >= 0 && coarse > 0.0)
        {
            // The coarse pass fires as soon as the window reaches past a
            // change, when the far side holds little data and the best
            // candidate is still a rough guess. The fine pass re-scores a
            // window of its own centred on that guess, so both sides weigh
            // in equally, and confirms or dismisses the change.
            const auto fineStart = std::max(a, coarseFrame - params.size2 / 2);
            const auto fineEnd = std::min(numFrames, coarseFrame + params.size2 / 2);
            int fineFrame;
            const auto fine = bestSplit(fineStart, fineEnd,
                                        std::max(fineStart + minLength, earliest), fineEnd - minLength,
                                        params.inc2, fineFrame);

            if (fineFrame >= 0 && fine > 0.0)
            {
                // Settle the frame: the fine step still leaves the boundary
                // up to half a step off, and the frames on the wrong side
                // would pass for another change once the next window
                // starts there.
                int exactFrame;
                bestSplit(fineStart, fineEnd,
                          std::max({ fineStart + minLength, earliest, fineFrame - params.inc2 }),
                          std::min(fineEnd - minLength, fineFrame + params.inc2),
                          1, exactFrame);
                if (exactFrame >= 0)
                    fineFrame = exactFrame;

                boundaries.push_back(fineFrame);
                a = fineFrame;
                b = std::min(numFrames, a + params.size1);
                continue;
            }
        }

        if (b >= numFrames)
            break;

        b = std::min(numFrames, b + params.inc1);
        if (b - a > params.maxWindow)
            a = b - params.maxWindow;
    }

    boundaries.push_back(numFrames);

    // Validation: each boundary must still separate its two neighbouring
    // segments once they are known in full. Dropping one widens the segments
    // next to it, so the pass repeats until nothing changes.
    auto changed = true;
    while (changed)
    {
        changed = false;
        for (size_t k = 1; k + 1 < boundaries.size(); ++k)
        {
            if (statistics.deltaBic(boundaries[k - 1], boundaries[k], boundaries[k + 1], penaltyWeight) <= 0.0)
            {
                boundaries.erase(boundaries.begin() + (long) k);
                changed = true;
                break;
            }
        }
    }

    // A trailing segment shorter than the minimum is not one.
    while (boundaries.size() > 2 && numFrames - boundaries[boundaries.size() - 2] < minSegment)
        boundaries.erase(boundaries.end() - 2);

    return boundaries;
}

} // namespace audium
