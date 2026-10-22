#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Engine/Analysis/SincResampler.h"

#include <atomic>
#include <cmath>

using namespace audium;

namespace {

std::vector<float> sine(double frequency, double sampleRate, double seconds, float amplitude = 0.5f)
{
    std::vector<float> signal((size_t) (sampleRate * seconds));
    for (size_t i = 0; i < signal.size(); ++i)
        signal[i] = amplitude * (float) std::sin(2.0 * juce::MathConstants<double>::pi * frequency * (double) i / sampleRate);
    return signal;
}

// RMS over the middle of the signal, clear of the kernel's reach at the ends.
float middleRms(const std::vector<float>& signal)
{
    const auto from = signal.size() / 4, to = signal.size() * 3 / 4;
    double sum = 0.0;
    for (auto i = from; i < to; ++i)
        sum += (double) signal[i] * signal[i];
    return (float) std::sqrt(sum / (double) (to - from));
}

float toDb(float ratio) { return 20.0f * std::log10(std::max(ratio, 1.0e-9f)); }

} // namespace

SCENARIO("SincResampler keeps the band and drops what would alias", "[engine][analysis][resampler]")
{
    GIVEN("a 44.1 kHz signal brought to 22.05 kHz")
    {
        const SincResampler resampler(44100.0, 22050.0);

        THEN("the length halves")
        {
            CHECK(resampler.outputLength(88200) == 44100);
            CHECK(resampler.outputLength(0) == 0);
            CHECK(resampler.process({}).empty());
        }

        THEN("tones inside the new band keep their level")
        {
            // up to 0.85 of the new Nyquist
            for (auto frequency : { 100.0, 1000.0, 5000.0, 8000.0, 9300.0 })
            {
                const auto in = sine(frequency, 44100.0, 1.0);
                const auto out = resampler.process(in);
                INFO(frequency << " Hz");
                CHECK(toDb(middleRms(out) / middleRms(in)) == Catch::Approx(0.0f).margin(0.1f));
            }
        }

        THEN("tones above the new Nyquist are gone")
        {
            for (auto frequency : { 12000.0, 15000.0, 20000.0 })
            {
                const auto in = sine(frequency, 44100.0, 1.0);
                const auto out = resampler.process(in);
                INFO(frequency << " Hz");
                CHECK(toDb(middleRms(out) / middleRms(in)) < -70.0f);
            }
        }

        THEN("DC passes at unity")
        {
            const std::vector<float> dc(44100, 0.25f);
            const auto out = resampler.process(dc);
            CHECK(out[out.size() / 2] == Catch::Approx(0.25f).margin(1.0e-4f));
        }

        THEN("a tone comes out as the same tone, sample for sample")
        {
            // 1 kHz at both rates: the output must equal the sine sampled at
            // 22.05 kHz directly, up to the kernel's precision.
            const auto out = resampler.process(sine(1000.0, 44100.0, 1.0));
            const auto expected = sine(1000.0, 22050.0, 1.0);
            float worst = 0.0f;
            for (size_t i = out.size() / 4; i < out.size() * 3 / 4; ++i)
                worst = std::max(worst, std::abs(out[i] - expected[i]));
            CHECK(worst < 1.0e-3f);
        }
    }

    GIVEN("a 48 kHz signal brought to 22.05 kHz")
    {
        const SincResampler resampler(48000.0, 22050.0);

        THEN("the length and a tone follow the ratio")
        {
            const auto out = resampler.process(sine(1000.0, 48000.0, 1.0));
            CHECK(out.size() == 22050);
            const auto expected = sine(1000.0, 22050.0, 1.0);
            float worst = 0.0f;
            for (size_t i = out.size() / 4; i < out.size() * 3 / 4; ++i)
                worst = std::max(worst, std::abs(out[i] - expected[i]));
            CHECK(worst < 1.0e-3f);
        }

        THEN("a tone above the new Nyquist is gone")
        {
            const auto in = sine(14000.0, 48000.0, 1.0);
            CHECK(toDb(middleRms(resampler.process(in)) / middleRms(in)) < -70.0f);
        }
    }

    GIVEN("an 8 kHz signal brought up to 22.05 kHz")
    {
        const SincResampler resampler(8000.0, 22050.0);

        THEN("a tone inside the input band keeps its level and nothing new appears")
        {
            const auto in = sine(1000.0, 8000.0, 1.0);
            const auto out = resampler.process(in);
            CHECK(out.size() == 22050);
            CHECK(toDb(middleRms(out) / middleRms(in)) == Catch::Approx(0.0f).margin(0.1f));
            const auto expected = sine(1000.0, 22050.0, 1.0);
            float worst = 0.0f;
            for (size_t i = out.size() / 4; i < out.size() * 3 / 4; ++i)
                worst = std::max(worst, std::abs(out[i] - expected[i]));
            CHECK(worst < 1.0e-3f);
        }
    }

    GIVEN("an abort flag already set")
    {
        const SincResampler resampler(44100.0, 22050.0);
        std::atomic<bool> abort { true };

        THEN("nothing comes back")
        {
            CHECK(resampler.process(sine(1000.0, 44100.0, 1.0), &abort).empty());
        }
    }
}
