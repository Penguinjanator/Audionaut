#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "Engine/Analysis/AnalysisAudioLoader.h"
#include "Engine/Analysis/Stft.h"
#include "Engine/Analysis/Mfcc.h"

#include <cmath>
#include <fstream>
#include <string>
#include <vector>

using namespace audium;

// The native MFCC frames held against Essentia's for the same recording.
//
// TestFiles/AnalysisReference/120-funk-export.mfcc.essentia.json was written
// by Essentia (capture-mfcc.cpp next to it has the code) and holds three
// sets of frames:
//
//  - "matchedFromResampledFile": Essentia configured the way the native
//    chain works - 22050 Hz, 2048-sample frames with a hop of 512, plain
//    Hann, Slaney mel with unit-area triangles, natural log, orthonormal
//    DCT-II - run on 120-funk-export.22050.wav, a 16-bit copy of the
//    recording at the analysis rate. With no resampler on either side the
//    two implementations compute the same numbers, and that is asserted
//    tightly: this is the proof that the native MFCC is Essentia's MFCC.
//
//  - "matched": the same configuration on the original 44.1 kHz file, so
//    the two resamplers are in the comparison. What remains is the loader:
//    JUCE's ResamplingAudioSource is a second-order Butterworth at the new
//    Nyquist, which rolls the top mel bands off by up to a decibel and lets
//    some energy alias into the top band, and the native loader removes DC
//    and peak-normalises (a constant offset in c0). Reported, and held to a
//    coarser bar.
//
//  - "production": the chain the segmenter ran on Essentia (44100 Hz, hop
//    1024, Blackman-Harris, HTK mel, dB), reported as a correlation only.
//    The frame rate is the same but nothing else is, and the BIC downstream
//    is invariant to linear maps of the feature vector anyway.

namespace {

using Frames = std::vector<std::vector<float>>;

Frames nativeFrames(const juce::File& audioFile, bool conditionSignal)
{
    AnalysisAudioLoader loader;
    AnalysisAudioLoader::Parameters loaderParams;
    loaderParams.targetSampleRate = 22050.0;
    loaderParams.removeDcOffset = conditionSignal;
    loaderParams.normalisePeak = conditionSignal;
    const auto audio = loader.load(audioFile, loaderParams);
    REQUIRE(audio);

    const Stft stft;
    Mfcc mfcc(Mfcc::Parameters(), stft.numBins(), audio->sampleRate);

    Frames frames((size_t) stft.numFrames(audio->samples.size()),
                  std::vector<float>((size_t) mfcc.getNumCoefficients()));
    stft.forEachPowerFrame(audio->samples, [&](int frame, const float* power)
    {
        mfcc.compute(power, frames[(size_t) frame].data());
    });
    return frames;
}

// Per coefficient: the constant offset between the two, the spread of the
// reference coefficient, the remaining difference once the offset is taken
// out as a fraction of that spread, and the correlation.
struct CoefficientComparison {
    float offset = 0.0f;
    float spread = 0.0f;
    float relativeResidual = 0.0f;
    float maxDifference = 0.0f;
    float correlation = 0.0f;
};

CoefficientComparison compareCoefficient(const Frames& reference, const Frames& candidate, size_t k, size_t skipEdges = 0)
{
    const auto n = std::min(reference.size(), candidate.size());
    CoefficientComparison result;
    if (n <= 2 * skipEdges)
        return result;

    double sumRef = 0.0, sumCand = 0.0;
    for (size_t t = skipEdges; t < n - skipEdges; ++t)
    {
        sumRef += reference[t][k];
        sumCand += candidate[t][k];
    }
    const auto count = (double) (n - 2 * skipEdges);
    const auto meanRef = sumRef / count;
    const auto meanCand = sumCand / count;
    result.offset = (float) (meanCand - meanRef);

    double varRef = 0.0, varCand = 0.0, covariance = 0.0, residual = 0.0, maxDifference = 0.0;
    for (size_t t = skipEdges; t < n - skipEdges; ++t)
    {
        const auto r = reference[t][k] - meanRef;
        const auto c = candidate[t][k] - meanCand;
        varRef += r * r;
        varCand += c * c;
        covariance += r * c;
        residual += (c - r) * (c - r);
        maxDifference = std::max(maxDifference, (double) std::abs(candidate[t][k] - reference[t][k]));
    }
    result.spread = (float) std::sqrt(varRef / count);
    result.relativeResidual = varRef > 0.0 ? (float) std::sqrt(residual / varRef) : 0.0f;
    result.maxDifference = (float) maxDifference;
    result.correlation = varRef > 0.0 && varCand > 0.0 ? (float) (covariance / std::sqrt(varRef * varCand)) : 0.0f;
    return result;
}

std::string fixed(float value, int decimals = 3)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, (double) value);
    return buffer;
}

} // namespace

SCENARIO("The native MFCC is Essentia's MFCC", "[engine][analysis][mfcc][reference]")
{
    const auto testFiles = juce::String(CURRENT_SOURCE_DIR) + "/TestFiles/";
    const std::string name = "120-funk-export";

    const juce::File fixtureFile(testFiles + "AnalysisReference/" + name + ".mfcc.essentia.json");
    if (! fixtureFile.existsAsFile())
        SKIP(name + ".mfcc.essentia.json is absent - see capture-mfcc.cpp next to it");

    std::ifstream stream(fixtureFile.getFullPathName().toStdString());
    const auto fixture = nlohmann::json::parse(stream);

    const juce::File originalFile(testFiles + fixture["audioFile"].get<std::string>());
    const juce::File resampledFile(testFiles + "AnalysisReference/" + fixture["resampledFile"].get<std::string>());
    if (! originalFile.existsAsFile() || originalFile.getSize() < 4096)
        SKIP(originalFile.getFileName() + " is absent or a Git LFS pointer");
    REQUIRE(resampledFile.existsAsFile());

    GIVEN("the recording at the analysis rate, so no resampler takes part")
    {
        const auto essentia = fixture["matchedFromResampledFile"].get<Frames>();
        const auto native = nativeFrames(resampledFile, false);

        REQUIRE_FALSE(essentia.empty());
        REQUIRE(essentia.front().size() == 13);
        REQUIRE(native.front().size() == 13);

        // Both centre their frames on the hop; Essentia emits one more frame
        // at the end, from padding alone.
        const auto n = std::min(essentia.size(), native.size());
        CHECK(essentia.size() >= native.size());
        CHECK(essentia.size() - native.size() <= 1);

        std::string report = "mfcc reference - " + name + " at 22050 Hz: " + std::to_string(essentia.size())
                           + " Essentia frames / " + std::to_string(native.size()) + " native frames; largest difference per coefficient:";
        float worst = 0.0f;
        for (size_t k = 0; k < 13; ++k)
        {
            const auto comparison = compareCoefficient(essentia, native, k);
            worst = std::max(worst, comparison.maxDifference);
            report += " c" + std::to_string(k) + " " + fixed(comparison.maxDifference, 5);
        }
        // The window the segmenter ran with on Essentia, Blackman-Harris 62,
        // on the same samples with everything else matched: what that choice
        // alone is worth, next to the loader's share reported further down.
        if (fixture.contains("blackmanHarrisFromResampledFile"))
        {
            const auto blackmanHarris = fixture["blackmanHarrisFromResampledFile"].get<Frames>();
            report += "\n  the same with Essentia's Blackman-Harris 62 window instead of Hann: per coefficient the offset, "
                      "the residual after the offset as a fraction of the coefficient's spread, and the correlation\n";
            for (size_t k = 0; k < 13; ++k)
            {
                const auto c = compareCoefficient(blackmanHarris, native, k, 2);
                report += "    c" + std::to_string(k) + (k < 10 ? " " : "") + ": offset " + fixed(c.offset) + " (spread "
                        + fixed(c.spread) + "), residual " + fixed(c.relativeResidual) + ", r = " + fixed(c.correlation, 4) + "\n";
            }
        }
        WARN(report);

        THEN("every coefficient of every frame agrees to float precision")
        {
            // The coefficients run from about -150 (near-silent frames) to
            // +100; the two implementations differ in float rounding only.
            for (size_t t = 0; t < n; ++t)
                for (size_t k = 0; k < 13; ++k)
                {
                    INFO("frame " << t << ", coefficient " << k);
                    REQUIRE(std::abs(native[t][k] - essentia[t][k]) < 0.001f);
                }
            CHECK(worst < 0.001f);
        }
    }

    GIVEN("the original 44.1 kHz recording, through each side's own loader")
    {
        const auto essentia = fixture["matched"]["mfcc"].get<Frames>();
        const auto production = fixture["production"]["mfcc"].get<Frames>();
        const auto native = nativeFrames(originalFile, true);

        REQUIRE_FALSE(native.empty());
        CHECK(std::abs((long) essentia.size() - (long) native.size()) <= 1);

        // The frames at either end are mostly padding and sit near the
        // floor, where the resamplers' edge behaviour dominates; left out.
        constexpr size_t edges = 2;

        std::string report = "mfcc reference - " + name + " from the original file: per coefficient the offset, the residual "
                             "after the offset as a fraction of the coefficient's spread, and the correlation\n";
        std::vector<CoefficientComparison> comparisons;
        for (size_t k = 0; k < 13; ++k)
        {
            comparisons.push_back(compareCoefficient(essentia, native, k, edges));
            const auto& c = comparisons.back();
            report += "    c" + std::to_string(k) + (k < 10 ? " " : "") + ": offset " + fixed(c.offset) + " (spread "
                    + fixed(c.spread) + "), residual " + fixed(c.relativeResidual) + ", r = " + fixed(c.correlation, 4) + "\n";
        }
        report += "  production configuration (Blackman-Harris, HTK mel, dB), correlation per coefficient:";
        for (size_t k = 0; k < 13; ++k)
            report += " " + fixed(compareCoefficient(production, native, k, edges).correlation, 3);
        WARN(report);

        THEN("the frames still describe the same spectra")
        {
            // What the loader costs: the first coefficients, which carry the
            // broad spectral shape, track closely; the high ones, which
            // resolve the top bands the resampler bends, less so.
            for (size_t k = 0; k < 5; ++k)
            {
                INFO("coefficient " << k);
                CHECK(comparisons[k].correlation > 0.97f);
                CHECK(comparisons[k].relativeResidual < 0.25f);
            }
            for (size_t k = 5; k < 13; ++k)
            {
                INFO("coefficient " << k);
                CHECK(comparisons[k].correlation > 0.85f);
            }
        }
    }
}
