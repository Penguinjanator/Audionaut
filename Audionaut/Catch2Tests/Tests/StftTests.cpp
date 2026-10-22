#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Engine/Analysis/Dsp/Stft.h"

#include <atomic>
#include <cmath>

using namespace audium;

namespace {

std::vector<float> sine(double frequency, double sampleRate, double seconds)
{
    std::vector<float> signal((size_t) (sampleRate * seconds));
    for (size_t i = 0; i < signal.size(); ++i)
        signal[i] = (float) std::sin(juce::MathConstants<double>::twoPi * frequency * (double) i / sampleRate);
    return signal;
}

} // namespace

SCENARIO("Stft counts frames by the hops whose centre lies inside the signal", "[engine][analysis][stft]")
{
    Stft::Parameters params;
    params.frameSize = 2048;
    params.hopSize = 512;
    const Stft stft(params);

    CHECK(stft.numBins() == 1025);
    CHECK(stft.numFrames(0) == 0);
    CHECK(stft.numFrames(1) == 1);
    CHECK(stft.numFrames(512) == 1);
    CHECK(stft.numFrames(513) == 2);
    CHECK(stft.numFrames(22050) == 44);

    CHECK(stft.binFrequency(0, 22050.0) == Catch::Approx(0.0));
    CHECK(stft.binFrequency(1024, 22050.0) == Catch::Approx(11025.0));
}

SCENARIO("Stft puts a sine's power into the right bin", "[engine][analysis][stft]")
{
    const Stft stft;
    const auto sampleRate = 22050.0;
    const auto signal = sine(1000.0, sampleRate, 1.0);

    GIVEN("the power spectra of a 1 kHz sine")
    {
        std::vector<int> peakBins;
        std::vector<float> dcPower;
        int framesSeen = 0;

        const auto completed = stft.forEachPowerFrame(signal, [&](int frame, const float* power)
        {
            CHECK(frame == framesSeen);
            ++framesSeen;

            auto peak = 0;
            for (int bin = 1; bin < stft.numBins(); ++bin)
                if (power[bin] > power[peak])
                    peak = bin;
            peakBins.push_back(peak);
            dcPower.push_back(power[0]);
        });

        THEN("every frame is visited")
        {
            CHECK(completed);
            CHECK(framesSeen == stft.numFrames(signal.size()));
        }

        THEN("the peak sits in the 1 kHz bin, except at the zero-padded edges")
        {
            const auto expected = (int) std::lround(1000.0 / stft.binFrequency(1, sampleRate));
            for (size_t frame = 2; frame + 2 < peakBins.size(); ++frame)
                CHECK(peakBins[frame] == expected);
        }

        THEN("there is next to no DC")
        {
            for (size_t frame = 2; frame + 2 < dcPower.size(); ++frame)
                CHECK(dcPower[frame] < 1.0e-3f);
        }
    }

    GIVEN("an abort flag already set")
    {
        std::atomic<bool> abort { true };
        int framesSeen = 0;
        const auto completed = stft.forEachPowerFrame(signal, [&](int, const float*) { ++framesSeen; }, &abort);

        THEN("no frame is produced and the run reports as abandoned")
        {
            CHECK_FALSE(completed);
            CHECK(framesSeen == 0);
        }
    }
}
