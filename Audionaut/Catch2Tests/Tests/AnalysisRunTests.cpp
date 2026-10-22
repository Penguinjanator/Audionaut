#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Engine/Analysis/AnalysisRun.h"
#include "Engine/Analysis/AnalysisProvider.h"
#include "Engine/Analysis/AnalysisWorker.h"
#include "Engine/Analysis/AnalysisAudioLoader.h"

#include <algorithm>
#include <atomic>
#include <chrono>

using namespace audium;

// One pass produces what the three single-type segmenters produce, and the
// provider hands it out from the cache afterwards.

namespace {

juce::File testFile()
{
    return juce::File(juce::String(CURRENT_SOURCE_DIR) + "/TestFiles/TRK-18-epy-jul.wav");
}

template <typename Fn>
double secondsOf(Fn&& fn)
{
    const auto started = std::chrono::steady_clock::now();
    fn();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
}

} // namespace

SCENARIO("AnalysisRun produces every analysis from one pass", "[engine][analysis][run]")
{
    const auto audioFile = testFile();
    REQUIRE(audioFile.existsAsFile());

    const AnalysisRun run;

    GIVEN("all three types asked for at once")
    {
        AnalysisRun::Result together;
        const auto onePass = secondsOf([&] { together = run.analyze(audioFile, AnalysisWorker::canonicalAnalysisTypes()); });

        std::vector<float> segments, onsets;
        BeatSegmenter::Result beats;
        const auto threePasses = secondsOf([&]
        {
            segments = SBicSegmenter().analyze(audioFile);
            beats = BeatSegmenter().analyze(audioFile);
            onsets = OnsetSegmenter().analyze(audioFile);
        });

        WARN("one pass for all three analyses: " << onePass << " s; the three separately: " << threePasses << " s");

        THEN("each result is set and equals the single-type segmenter's")
        {
            REQUIRE(together.segments.has_value());
            REQUIRE(together.beats.has_value());
            REQUIRE(together.onsets.has_value());
            CHECK(*together.segments == segments);
            CHECK(together.beats->beats == beats.beats);
            CHECK(together.beats->bpm == Catch::Approx(beats.bpm));
            CHECK(*together.onsets == onsets);
        }
    }

    GIVEN("a progress callback")
    {
        std::vector<double> fractions;
        run.analyze(audioFile, AnalysisWorker::canonicalAnalysisTypes(), nullptr,
                    [&fractions](double fraction) { fractions.push_back(fraction); });

        THEN("it is told the pass's progress from 0 to 1, never going back")
        {
            REQUIRE(fractions.size() >= 5);
            CHECK(fractions.front() == 0.0);
            CHECK(fractions.back() == 1.0);
            CHECK(std::is_sorted(fractions.begin(), fractions.end()));
        }
    }

    GIVEN("only the beats asked for")
    {
        const auto result = run.analyze(audioFile, { AnalysisType::Beats, AnalysisType::Beats });

        THEN("only the beats come back")
        {
            CHECK_FALSE(result.segments.has_value());
            CHECK(result.beats.has_value());
            CHECK_FALSE(result.onsets.has_value());
        }
    }

    GIVEN("no types")
    {
        THEN("nothing comes back")
        {
            const auto result = run.analyze(audioFile, {});
            CHECK_FALSE(result.segments.has_value());
            CHECK_FALSE(result.beats.has_value());
            CHECK_FALSE(result.onsets.has_value());
        }
    }

    GIVEN("an abort flag already set")
    {
        std::atomic<bool> abort { true };

        THEN("nothing comes back")
        {
            const auto result = run.analyze(audioFile, AnalysisWorker::canonicalAnalysisTypes(), &abort);
            CHECK_FALSE(result.segments.has_value());
            CHECK_FALSE(result.beats.has_value());
            CHECK_FALSE(result.onsets.has_value());
        }
    }

    GIVEN("a file that cannot be decoded")
    {
        const auto bogus = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("AudionautTests").getChildFile("not-audio.wav");
        bogus.getParentDirectory().createDirectory();
        bogus.replaceWithText("this is not a wav file");

        THEN("the loader says why and the pass yields nothing")
        {
            CHECK(AnalysisAudioLoader::describeProblem(bogus).isNotEmpty());
            CHECK(AnalysisAudioLoader::describeProblem(audioFile).isEmpty());
            const auto result = run.analyze(bogus, AnalysisWorker::canonicalAnalysisTypes());
            CHECK_FALSE(result.segments.has_value());
        }

        bogus.deleteFile();
    }
}

SCENARIO("AnalysisProvider analyses several types in one pass and caches each", "[engine][analysis][run]")
{
    const auto audioFile = testFile();
    REQUIRE(audioFile.existsAsFile());

    auto cache = std::make_shared<AnalysisCache>();
    auto provider = std::make_shared<AnalysisProvider>(cache);

    WHEN("segments and beats are asked for together")
    {
        const auto results = provider->analyzeFile(audioFile, { AnalysisType::Segments, AnalysisType::Beats });

        THEN("both are returned and cached, with the beats' tempo")
        {
            REQUIRE(results.count(AnalysisType::Segments) == 1);
            REQUIRE(results.count(AnalysisType::Beats) == 1);
            CHECK(results.count(AnalysisType::Onsets) == 0);
            CHECK(cache->get(audioFile, AnalysisType::Segments) == results.at(AnalysisType::Segments));
            CHECK(cache->get(audioFile, AnalysisType::Beats) == results.at(AnalysisType::Beats));
            CHECK(provider->getBpm(AnalysisType::Beats, audioFile) > 0.0f);
            CHECK(cache->size() == 2);
        }

        THEN("a later request for all three only runs the onsets")
        {
            const auto before = cache->size();
            const auto all = provider->analyzeFile(audioFile, AnalysisWorker::canonicalAnalysisTypes());
            CHECK(all.size() == 3);
            CHECK(cache->size() == before + 1);
            CHECK(all.at(AnalysisType::Segments) == results.at(AnalysisType::Segments));
        }
    }
}
