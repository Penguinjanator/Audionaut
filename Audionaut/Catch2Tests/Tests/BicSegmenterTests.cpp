#include <catch2/catch_test_macros.hpp>

#include "Engine/Analysis/BicSegmenter.h"

#include <atomic>
#include <cmath>

using namespace audium;

namespace {

constexpr int dimensions = 13;

// Frames drawn from a Gaussian with the given mean in every dimension and
// unit variance, by a seeded generator so the scenario is repeatable.
void appendGaussianFrames(std::vector<std::vector<float>>& features, int numFrames, float mean, juce::Random& random)
{
    for (int t = 0; t < numFrames; ++t)
    {
        std::vector<float> frame((size_t) dimensions);
        for (auto& value : frame)
        {
            // Box-Muller
            const auto u1 = std::max(1.0e-9, random.nextDouble());
            const auto u2 = random.nextDouble();
            value = mean + (float) (std::sqrt(-2.0 * std::log(u1)) * std::cos(juce::MathConstants<double>::twoPi * u2));
        }
        features.push_back(std::move(frame));
    }
}

std::string describe(const std::vector<int>& boundaries)
{
    std::string text;
    for (auto boundary : boundaries)
        text += (text.empty() ? "" : ", ") + std::to_string(boundary);
    return text;
}

} // namespace

SCENARIO("BicSegmenter finds where a feature sequence changes character", "[engine][analysis][bic]")
{
    const BicSegmenter segmenter;
    juce::Random random(42);

    GIVEN("two stretches with different means")
    {
        std::vector<std::vector<float>> features;
        appendGaussianFrames(features, 600, 0.0f, random);
        appendGaussianFrames(features, 600, 2.0f, random);

        const auto boundaries = segmenter.segment(features);

        THEN("there is exactly one boundary, at the change, between the two ends")
        {
            INFO("boundaries: " << describe(boundaries));
            REQUIRE(boundaries.size() == 3);
            CHECK(boundaries.front() == 0);
            CHECK(boundaries.back() == 1200);
            CHECK(std::abs(boundaries[1] - 600) <= 30);
        }
    }

    GIVEN("three stretches, the outer two alike")
    {
        std::vector<std::vector<float>> features;
        appendGaussianFrames(features, 500, 0.0f, random);
        appendGaussianFrames(features, 500, 2.0f, random);
        appendGaussianFrames(features, 500, 0.0f, random);

        const auto boundaries = segmenter.segment(features);

        THEN("both changes are found")
        {
            INFO("boundaries: " << describe(boundaries));
            REQUIRE(boundaries.size() == 4);
            CHECK(std::abs(boundaries[1] - 500) <= 30);
            CHECK(std::abs(boundaries[2] - 1000) <= 30);
            CHECK(boundaries.back() == 1500);
        }
    }

    GIVEN("one homogeneous stretch")
    {
        std::vector<std::vector<float>> features;
        appendGaussianFrames(features, 1200, 0.0f, random);

        const auto boundaries = segmenter.segment(features);

        THEN("only the two ends come back")
        {
            INFO("boundaries: " << describe(boundaries));
            CHECK(boundaries == std::vector<int> { 0, 1200 });
        }
    }

    GIVEN("no frames")
    {
        THEN("nothing comes back")
        {
            CHECK(segmenter.segment({}).empty());
        }
    }

    GIVEN("an abort flag already set")
    {
        std::vector<std::vector<float>> features;
        appendGaussianFrames(features, 600, 0.0f, random);
        appendGaussianFrames(features, 600, 2.0f, random);

        std::atomic<bool> abort { true };

        THEN("the segmentation is abandoned")
        {
            CHECK(segmenter.segment(features, BicSegmenter::Parameters(), &abort).empty());
        }
    }
}
