#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "AnalysisReference.h"

#include "Engine/Analysis/SBicSegmenter.h"
#include "Engine/Analysis/BeatSegmenter.h"
#include "Engine/Analysis/OnsetSegmenter.h"

#include <chrono>

using namespace audium;
using namespace analysisReference;

// The analysis ran on Essentia until it was replaced by the segmenters in
// Source/Engine/Analysis. The fixtures under TestFiles/AnalysisReference
// record what Essentia produced for the test recordings (see capture.sh
// there), and this scenario holds the segmenters against them. Essentia
// itself passed it at 100 %, which is what makes the tolerances in
// AnalysisReference.h meaningful: a change that drifts past them changes
// what Auto Edit, the grid snapping and the clip tempo see.

namespace {

template <typename Fn>
auto timed(Fn&& fn, double& seconds)
{
    const auto started = std::chrono::steady_clock::now();
    auto result = fn();
    seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return result;
}

} // namespace

SCENARIO("The analysis reproduces the recorded reference results", "[engine][analysis][reference]")
{
    for (const auto& name : recordings)
    {
        DYNAMIC_SECTION(name)
        {
            Fixture fixture;
            if (auto reason = load(name, fixture))
                SKIP(*reason);

            double segmentSeconds = 0.0, beatSeconds = 0.0, onsetSeconds = 0.0;

            SBicSegmenter sbic;
            const auto segments = timed([&] { return sbic.analyze(fixture.audioFile); }, segmentSeconds);

            BeatSegmenter beatSegmenter;
            const auto rhythm = timed([&] { return beatSegmenter.analyze(fixture.audioFile); }, beatSeconds);

            OnsetSegmenter onsetSegmenter;
            const auto onsets = timed([&] { return onsetSegmenter.analyze(fixture.audioFile); }, onsetSeconds);

            const auto beatF = fMeasure(fixture.beats, rhythm.beats, beatToleranceSeconds);
            const auto onsetF = fMeasure(fixture.onsets, onsets, onsetToleranceSeconds);
            const SegmentComparison comparison(fixture.segments, segments);

            // Reported on every run: how close the two implementations are is
            // the point of the scenario, not only whether they clear the bar.
            WARN("analysis reference - " << name << " (" << fixture.duration << " s; analysed in "
                 << segmentSeconds << " + " << beatSeconds << " + " << onsetSeconds << " s)\n"
                 << "  beats: " << fixture.beats.size() << " reference / " << rhythm.beats.size()
                 << " candidate, F = " << beatF << " at " << beatToleranceSeconds << " s\n"
                 << "  bpm: " << fixture.bpm << " reference / " << rhythm.bpm << " candidate\n"
                 << comparison.report(fixture.segments, segments) << "\n"
                 << "  onsets: " << fixture.onsets.size() << " reference / " << onsets.size()
                 << " candidate, F = " << onsetF << " at " << onsetToleranceSeconds << " s");

            // Checked in one pass: Catch2 re-runs the section per leaf
            // assertion block, and the analyses are the expensive part.

            // Beats and tempo - what Auto Edit's merge, grid snapping and the
            // clip tempo build on.
            CHECK_FALSE(rhythm.beats.empty());
            CHECK(beatF >= minimumBeatFMeasure);
            CHECK(rhythm.bpm > 0.0f);
            CHECK(tempoAgrees(rhythm.bpm, fixture.bpm));

            // Segments - the sparse stream of the merge. The list always spans
            // the material: first boundary at 0, last at the end.
            REQUIRE(segments.size() >= 2);
            CHECK(segments.front() == Catch::Approx(0.0f).margin(0.001f));
            CHECK(segments.back() == Catch::Approx((float) fixture.duration).margin(0.1f));
            CHECK(std::is_sorted(segments.begin(), segments.end()));

            if (! comparison.referenceInterior.empty())
            {
                CHECK(comparison.hits >= minimumSegmentAgreement);
                CHECK(comparison.hitsReverse >= minimumSegmentAgreement);
                CHECK_FALSE(comparison.candidateInterior.empty());
                CHECK(comparison.countRatio <= maximumSegmentCountRatio);
            }

            // Onsets - display only, held to a looser bar.
            CHECK_FALSE(onsets.empty());
            CHECK(onsetF >= minimumOnsetFMeasure);
        }
    }
}
