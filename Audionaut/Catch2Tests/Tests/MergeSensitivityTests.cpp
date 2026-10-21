#include <catch2/catch_test_macros.hpp>

#include "AnalysisReference.h"

#include "Engine/Analysis/AnalysisProvider.h"
#include "Engine/Analysis/EventMerger.h"
#include "Engine/Analysis/SBicSegmenter.h"
#include "Engine/Analysis/BeatSegmenter.h"
#include "Engine/AutoEdit/AutoEditParameter.h"

#include <algorithm>
#include <random>
#include <string>

using namespace audium;
using namespace analysisReference;

// Hidden: how far the Auto Edit cuts move when the segmentation they are
// merged from moves a little. The recorded Essentia boundaries are merged as
// they are, then with every interior boundary jittered by a few hundred
// milliseconds, then replaced by the native segmentation; each time the cuts
// are held against the unjittered ones. If jitter alone moves the cuts as
// much as the native segmentation does, cut-level equality is not a property
// the merge ever had, and the bar for the native segmentation is the
// boundaries, not the cuts. Run with "[merge-jitter]" and the compact
// reporter.

namespace {

// The merge parameters Auto Edit derives for a target segment length in
// measures (see effectiveMergeParameters in AutoEdit.cpp).
EventMerger::Parameters mergeParameters(double durationSeconds, double bpm, double measures)
{
    EventMerger::Parameters params;
    const AutoEditParameter parameter(measures);

    if (parameter.isActive())
    {
        if (const auto derived = parameter.numSegmentsFor(durationSeconds, bpm); derived > 0)
            params.numSegments = derived;

        if (const auto minSeconds = parameter.minSegmentSeconds(bpm); minSeconds > 0.0)
            params.minSegmentFrames = EventMerger::timeToFrame((float) minSeconds, params);
    }

    return params;
}

std::vector<float> cutsOf(const std::vector<float>& segments,
                          const std::vector<float>& beats,
                          double durationSeconds,
                          const EventMerger::Parameters& params)
{
    const auto streams = AnalysisProvider::makeMergeStreams(segments, beats);
    EventMerger merger;
    auto cuts = merger.merge(streams, (float) durationSeconds, params).boundaries;

    // the merge opens every run at zero, which is no cut
    if (! cuts.empty() && cuts.front() <= 0.0f)
        cuts.erase(cuts.begin());

    return cuts;
}

// The interior boundaries moved by Gaussian noise, kept inside the material
// and in order; the first and last boundary stay put.
std::vector<float> jittered(const std::vector<float>& boundaries, float sigmaSeconds, std::mt19937& rng)
{
    if (boundaries.size() < 3)
        return boundaries;

    std::normal_distribution<float> noise(0.0f, sigmaSeconds);
    auto moved = boundaries;
    for (size_t i = 1; i + 1 < moved.size(); ++i)
        moved[i] = std::clamp(boundaries[i] + noise(rng), 0.0f, boundaries.back());
    std::sort(moved.begin() + 1, moved.end() - 1);
    return moved;
}

struct CutAgreement {
    float baselineFound = 0.0f;   // baseline cuts within tolerance of a candidate cut
    float candidateFound = 0.0f;  // candidate cuts within tolerance of a baseline cut
    size_t candidateCount = 0;
};

CutAgreement compare(const std::vector<float>& baseline, const std::vector<float>& candidate, float tolerance)
{
    return { agreement(baseline, candidate, tolerance), agreement(candidate, baseline, tolerance), candidate.size() };
}

std::string percent(float fraction)
{
    return std::to_string((int) std::lround(fraction * 100.0f)) + " %";
}

} // namespace

SCENARIO("Merge sensitivity to the segmentation", "[.][merge-jitter]")
{
    constexpr auto tolerance = 1.0f;
    constexpr auto trials = 20;

    for (const auto& name : recordings)
    {
        Fixture fixture;
        if (load(name, fixture))
            continue;

        // Both native analyses, so the beats' share of the difference is
        // visible next to the segmentation's.
        SBicSegmenter sbic;
        const auto nativeSegments = sbic.analyze(fixture.audioFile);
        BeatSegmenter beatSegmenter;
        const auto nativeRhythm = beatSegmenter.analyze(fixture.audioFile);

        for (auto measures : { 4.0, 0.0 })
        {
            const auto params = mergeParameters(fixture.duration, fixture.bpm, measures);
            const auto baseline = cutsOf(fixture.segments, fixture.beats, fixture.duration, params);

            std::string report = name + " at " + (measures > 0.0 ? std::to_string((int) measures) + " measures"
                                                                 : std::string("the default parameters"))
                               + ": " + std::to_string(baseline.size()) + " cuts from Essentia's own analyses\n";

            std::mt19937 rng(7);
            for (auto sigma : { 0.1f, 0.25f, 0.5f })
            {
                float sumBaseline = 0.0f, sumCandidate = 0.0f, minBaseline = 1.0f;
                size_t sumCount = 0;
                for (int trial = 0; trial < trials; ++trial)
                {
                    const auto moved = jittered(fixture.segments, sigma, rng);
                    const auto result = compare(baseline, cutsOf(moved, fixture.beats, fixture.duration, params), tolerance);
                    sumBaseline += result.baselineFound;
                    sumCandidate += result.candidateFound;
                    minBaseline = std::min(minBaseline, result.baselineFound);
                    sumCount += result.candidateCount;
                }
                report += "  Essentia boundaries jittered by " + std::to_string(sigma).substr(0, 4) + " s: "
                        + percent(sumBaseline / trials) + " of the cuts kept (worst run " + percent(minBaseline)
                        + "), " + percent(sumCandidate / trials) + " of the new cuts were there before, "
                        + std::to_string(sumCount / (size_t) trials) + " cuts on average\n";
            }

            const auto nativeSegmentsOnly = compare(baseline, cutsOf(nativeSegments, fixture.beats, fixture.duration, params), tolerance);
            const auto nativeBoth = compare(baseline, cutsOf(nativeSegments, nativeRhythm.beats, fixture.duration, params), tolerance);
            report += "  native segmentation with Essentia's beats: " + percent(nativeSegmentsOnly.baselineFound)
                    + " of the cuts kept, " + percent(nativeSegmentsOnly.candidateFound) + " of the new cuts were there before, "
                    + std::to_string(nativeSegmentsOnly.candidateCount) + " cuts\n";
            report += "  native segmentation and native beats:      " + percent(nativeBoth.baselineFound)
                    + " of the cuts kept, " + percent(nativeBoth.candidateFound) + " of the new cuts were there before, "
                    + std::to_string(nativeBoth.candidateCount) + " cuts";

            WARN(report);
        }
    }
}
