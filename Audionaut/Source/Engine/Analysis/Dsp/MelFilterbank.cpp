//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "MelFilterbank.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace audium {

namespace {

// Slaney's scale: 200/3 Hz per mel up to 1 kHz (15 mel), then a log scale
// that spans 27 mel per factor 6.4 in frequency.
constexpr double hertzPerMel = 200.0 / 3.0;
constexpr double logStartHertz = 1000.0;
constexpr double logStartMel = logStartHertz / hertzPerMel;
const double logStep = std::log(6.4) / 27.0;

} // namespace

double MelFilterbank::hertzToMel(double hertz)
{
    if (hertz < logStartHertz)
        return hertz / hertzPerMel;
    return logStartMel + std::log(hertz / logStartHertz) / logStep;
}

double MelFilterbank::melToHertz(double mel)
{
    if (mel < logStartMel)
        return mel * hertzPerMel;
    return logStartHertz * std::exp(logStep * (mel - logStartMel));
}

MelFilterbank::MelFilterbank(int numBandsToUse, int numBinsToUse, double sampleRate,
                             double lowFrequency, double highFrequency)
    : numBins(numBinsToUse)
{
    jassert(numBandsToUse > 0 && numBinsToUse > 1 && sampleRate > 0.0);

    if (highFrequency <= 0.0)
        highFrequency = sampleRate / 2.0;

    // numBands + 2 edge frequencies, equally spaced in mel
    const auto lowMel = hertzToMel(lowFrequency);
    const auto highMel = hertzToMel(highFrequency);
    std::vector<double> edges((size_t) numBandsToUse + 2);
    for (size_t i = 0; i < edges.size(); ++i)
        edges[i] = melToHertz(lowMel + (highMel - lowMel) * (double) i / (double) (numBandsToUse + 1));

    // bins are frameSize wide in frequency terms: (numBins - 1) * 2
    const auto binWidth = sampleRate / (double) ((numBins - 1) * 2);

    filters.resize((size_t) numBandsToUse);
    centres.resize((size_t) numBandsToUse);

    for (int band = 0; band < numBandsToUse; ++band)
    {
        const auto lower = edges[(size_t) band];
        const auto centre = edges[(size_t) band + 1];
        const auto upper = edges[(size_t) band + 2];
        centres[(size_t) band] = centre;

        // unit area: 2 / width
        const auto normalisation = 2.0 / (upper - lower);

        auto& filter = filters[(size_t) band];
        const auto firstBin = std::max(0, (int) std::ceil(lower / binWidth));
        const auto lastBin = std::min(numBins - 1, (int) std::floor(upper / binWidth));
        filter.firstBin = firstBin;

        for (int bin = firstBin; bin <= lastBin; ++bin)
        {
            const auto frequency = (double) bin * binWidth;
            const auto rising = (frequency - lower) / (centre - lower);
            const auto falling = (upper - frequency) / (upper - centre);
            const auto weight = std::max(0.0, std::min(rising, falling)) * normalisation;
            filter.weights.push_back((float) weight);
        }

        // Narrow filters at the low end can fall between two bins; a filter
        // that weighs nothing would silence the band. Give it the nearest bin.
        const auto total = std::accumulate(filter.weights.begin(), filter.weights.end(), 0.0f);
        if (total <= 0.0f)
        {
            filter.firstBin = std::clamp((int) std::lround(centre / binWidth), 0, numBins - 1);
            filter.weights.assign(1, (float) normalisation);
        }
    }
}

void MelFilterbank::apply(const float* power, float* bands) const
{
    for (size_t band = 0; band < filters.size(); ++band)
    {
        const auto& filter = filters[band];
        const auto* in = power + filter.firstBin;
        float sum = 0.0f;
        for (size_t i = 0; i < filter.weights.size(); ++i)
            sum += in[i] * filter.weights[i];
        bands[band] = sum;
    }
}

} // namespace audium
