#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "AnalysisReference.h"

#include "Engine/Analysis/SBicSegmenter.h"

#include <atomic>
#include <chrono>
#include <cmath>

using namespace audium;
using namespace analysisReference;

// The native segmentation (AnalysisAudioLoader -> Stft -> Mfcc ->
// BicSegmenter) held against synthetic material and against what Essentia
// produced for the test recordings. Runs in every build, with or without
// Essentia.

namespace {

SBicSegmenter::Parameters nativeParameters()
{
    return SBicSegmenter::nativeDefaults();
}

// Writes white noise coloured one way for the first half and the other way
// for the second: a one-pole low-pass of the noise, then what it took away.
juce::File writeTwoColoursOfNoise(double sampleRate, double secondsPerHalf)
{
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("AudionautTests")
                          .getChildFile("two-colours-of-noise.wav");
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    REQUIRE(stream != nullptr);
    auto options = juce::AudioFormatWriter::Options{}.withSampleRate(sampleRate)
                       .withNumChannels(1)
                       .withBitsPerSample(16);
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream, options));
    REQUIRE(writer != nullptr);

    juce::Random random(7);
    const auto numSamples = (int) (sampleRate * secondsPerHalf);
    juce::AudioBuffer<float> buffer(1, numSamples);
    auto* out = buffer.getWritePointer(0);

    float lowPassed = 0.0f;
    for (int half = 0; half < 2; ++half)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto noise = random.nextFloat() * 2.0f - 1.0f;
            lowPassed += 0.05f * (noise - lowPassed);
            out[i] = half == 0 ? lowPassed * 2.0f : (noise - lowPassed) * 0.5f;
        }
        writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
    }
    return file;
}

} // namespace

SCENARIO("The native segmentation cuts where the sound changes", "[engine][analysis][segmentation][native]")
{
    SBicSegmenter segmenter;

    GIVEN("fifteen seconds of one noise colour followed by fifteen of another")
    {
        const auto file = writeTwoColoursOfNoise(44100.0, 15.0);
        const auto segments = segmenter.analyzeNative(file, nativeParameters());

        THEN("the boundaries are the ends and the one change")
        {
            INFO("segments: " << describe(segments));
            REQUIRE(segments.size() == 3);
            CHECK(segments.front() == Catch::Approx(0.0f).margin(0.001f));
            CHECK(segments[1] == Catch::Approx(15.0f).margin(1.0f));
            CHECK(segments.back() == Catch::Approx(30.0f).margin(0.1f));
        }

        file.deleteFile();
    }

    GIVEN("a missing file")
    {
        THEN("nothing comes back")
        {
            CHECK(segmenter.analyzeNative(juce::File("/non/existent/audio/file.wav"), nativeParameters()).empty());
        }
    }

    GIVEN("an abort flag already set")
    {
        const auto file = writeTwoColoursOfNoise(44100.0, 2.0);
        std::atomic<bool> abort { true };

        THEN("the analysis is abandoned")
        {
            CHECK(segmenter.analyzeNative(file, nativeParameters(), &abort).empty());
        }

        file.deleteFile();
    }
}

// Hidden: reports how the agreement with the reference moves with the
// complexity penalty, with and without the small-sample correction, for
// tuning SBicSegmenter::nativeDefaults(). Run with "[bic-sweep]"; the
// compact reporter keeps the boundary lists on one line each.
SCENARIO("Native segmentation penalty sweep", "[.][bic-sweep]")
{
    for (auto correct : { true, false })
    for (auto penalty : { 0.5f, 0.6f, 0.65f, 0.7f, 0.75f, 1.0f })
    {
        std::string report = std::string(correct ? "corrected" : "uncorrected") + " penalty " + std::to_string(penalty) + "\n";
        for (const auto& name : recordings)
        {
            Fixture fixture;
            if (load(name, fixture))
                continue;

            auto params = nativeParameters();
            params.complexityPenaltyWeight = penalty;
            params.correctSmallSampleBias = correct;
            SBicSegmenter segmenter;
            const auto segments = segmenter.analyzeNative(fixture.audioFile, params);
            const SegmentComparison comparison(fixture.segments, segments);
            // reference / candidate interior boundaries, then the agreement
            // in each direction
            report += "  " + name.substr(0, 14) + ": " + std::to_string(comparison.referenceInterior.size()) + "/"
                    + std::to_string(comparison.candidateInterior.size()) + " hits "
                    + std::to_string((int) (comparison.hits * 100.0f)) + " rev "
                    + std::to_string((int) (comparison.hitsReverse * 100.0f)) + "\n";
            if (fixture.duration < 100.0)
                report += "    " + describe(segments) + "\n";
        }
        WARN(report);
    }
}

SCENARIO("The native segmentation agrees with the recorded reference", "[engine][analysis][reference][native]")
{
    for (const auto& name : recordings)
    {
        DYNAMIC_SECTION(name)
        {
            Fixture fixture;
            if (auto reason = load(name, fixture))
                SKIP(*reason);

            SBicSegmenter segmenter;
            const auto started = std::chrono::steady_clock::now();
            const auto segments = segmenter.analyzeNative(fixture.audioFile, nativeParameters());
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

            const SegmentComparison comparison(fixture.segments, segments);

            WARN("native segmentation - " << name << " (" << fixture.duration << " s, analysed in " << elapsed << " s)\n"
                 << comparison.report(fixture.segments, segments));

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
        }
    }
}
