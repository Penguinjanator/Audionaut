#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "AnalysisReference.h"

#include "Engine/Analysis/SBicSegmenter.h"
#include "Engine/Analysis/BeatSegmenter.h"
#include "Engine/Analysis/OnsetSegmenter.h"

// Until the native analysis lands, a build without Essentia has no analysis
// at all - nothing to compare. See ESSENTIA_ENABLED in the segmenters.
#ifndef ESSENTIA_ENABLED
 #if __has_include(<essentia/algorithmfactory.h>) && __has_include(<unsupported/Eigen/CXX11/Tensor>)
  #define ESSENTIA_ENABLED 1
 #else
  #define ESSENTIA_ENABLED 0
 #endif
#endif

using namespace audium;
using namespace analysisReference;

// The analysis is being moved off Essentia. The fixtures under
// TestFiles/AnalysisReference record what the Essentia-backed segmenters
// produced for the test recordings (see capture.sh there), and this scenario
// holds whichever implementation is built against them. It must pass at 100 %
// with Essentia itself - that is what makes the tolerances in
// AnalysisReference.h meaningful for the native implementation that follows.

SCENARIO("The analysis reproduces the recorded reference results", "[engine][analysis][reference]")
{
    if (! ESSENTIA_ENABLED)
        SKIP("this build has no analysis backend");

    for (const auto& name : recordings)
    {
        DYNAMIC_SECTION(name)
        {
            Fixture fixture;
            if (auto reason = load(name, fixture))
                SKIP(*reason);

            SBicSegmenter sbic;
            const auto segments = sbic.analyze(fixture.audioFile);

            BeatSegmenter beatSegmenter;
            BeatSegmenter::Parameters beatParams;
            beatParams.method = BeatSegmenter::Method::Degara;
            const auto rhythm = beatSegmenter.analyze(fixture.audioFile, beatParams);

            OnsetSegmenter onsetSegmenter;
            const auto onsets = onsetSegmenter.analyze(fixture.audioFile);

            const auto beatF = fMeasure(fixture.beats, rhythm.beats, beatToleranceSeconds);
            const auto onsetF = fMeasure(fixture.onsets, onsets, onsetToleranceSeconds);
            const SegmentComparison comparison(fixture.segments, segments);

            // Reported on every run: how close the two implementations are is
            // the point of the scenario, not only whether they clear the bar.
            WARN("analysis reference - " << name << " (" << fixture.duration << " s)\n"
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
