#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Engine/Analysis/MelFilterbank.h"
#include "Engine/Analysis/Mfcc.h"

#include <cmath>

using namespace audium;

SCENARIO("MelFilterbank follows Slaney's mel scale", "[engine][analysis][mfcc]")
{
    THEN("the scale is linear up to 1 kHz, 15 mel there, and invertible")
    {
        CHECK(MelFilterbank::hertzToMel(0.0) == Catch::Approx(0.0));
        CHECK(MelFilterbank::hertzToMel(500.0) == Catch::Approx(7.5));
        CHECK(MelFilterbank::hertzToMel(1000.0) == Catch::Approx(15.0));
        CHECK(MelFilterbank::hertzToMel(6400.0) == Catch::Approx(42.0));

        for (auto hertz : { 50.0, 440.0, 1000.0, 3000.0, 11025.0 })
            CHECK(MelFilterbank::melToHertz(MelFilterbank::hertzToMel(hertz)) == Catch::Approx(hertz));
    }

    GIVEN("40 filters over 1025 bins at 22050 Hz")
    {
        const auto numBins = 1025;
        const auto sampleRate = 22050.0;
        const MelFilterbank filterbank(40, numBins, sampleRate);

        THEN("the centres rise from above 0 Hz to below Nyquist")
        {
            CHECK(filterbank.getNumBands() == 40);
            CHECK(filterbank.bandFrequency(0) > 0.0);
            CHECK(filterbank.bandFrequency(39) < sampleRate / 2.0);
            for (int band = 1; band < 40; ++band)
                CHECK(filterbank.bandFrequency(band) > filterbank.bandFrequency(band - 1));
        }

        THEN("a flat spectrum gives every band the same energy: one per bin width")
        {
            std::vector<float> power((size_t) numBins, 1.0f);
            std::vector<float> bands(40);
            filterbank.apply(power.data(), bands.data());

            const auto binWidth = sampleRate / 2048.0;
            for (auto band : bands)
                CHECK(band == Catch::Approx(1.0 / binWidth).epsilon(0.2));
        }
    }
}

SCENARIO("Mfcc is an orthonormal DCT of the log band energies", "[engine][analysis][mfcc]")
{
    Mfcc::Parameters params;
    params.numBands = 40;
    params.numCoefficients = 13;
    Mfcc mfcc(params, 1025, 22050.0);

    GIVEN("band energies whose log is one cosine cycle across the bands")
    {
        const auto numBands = 40;
        std::vector<float> bands((size_t) numBands);
        for (int m = 0; m < numBands; ++m)
            bands[(size_t) m] = std::exp(std::cos(juce::MathConstants<float>::pi * ((float) m + 0.5f) / (float) numBands));

        std::vector<float> coefficients(13);
        mfcc.computeFromBands(bands.data(), coefficients.data());

        THEN("only the first coefficient responds, with the orthonormal gain")
        {
            CHECK(coefficients[1] == Catch::Approx(std::sqrt(numBands / 2.0)).epsilon(0.001));
            for (int n = 0; n < 13; ++n)
                if (n != 1)
                    CHECK(std::abs(coefficients[(size_t) n]) < 1.0e-3f);
        }
    }

    GIVEN("a flat spectrum")
    {
        std::vector<float> power(1025, 1.0f);
        std::vector<float> coefficients(13);
        mfcc.compute(power.data(), coefficients.data());

        THEN("the level shows in the zeroth coefficient and little else")
        {
            CHECK(coefficients[0] < 0.0f);   // log of energies below 1
            for (int n = 1; n < 13; ++n)
                CHECK(std::abs(coefficients[(size_t) n]) < 0.1f * std::abs(coefficients[0]));
        }
    }

    GIVEN("silence")
    {
        std::vector<float> power(1025, 0.0f);
        std::vector<float> coefficients(13);
        mfcc.compute(power.data(), coefficients.data());

        THEN("the floor keeps the log finite")
        {
            for (auto coefficient : coefficients)
                CHECK(std::isfinite(coefficient));
        }
    }
}
