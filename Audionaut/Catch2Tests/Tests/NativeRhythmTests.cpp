#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "AnalysisReference.h"

#include "Engine/Analysis/BeatSegmenter.h"
#include "Engine/Analysis/OnsetSegmenter.h"
#include "Engine/Analysis/AnalysisAudioLoader.h"
#include "Engine/Analysis/Stft.h"
#include "Engine/Analysis/OnsetStrength.h"
#include "Engine/Analysis/TempoEstimator.h"

#include <atomic>
#include <chrono>
#include <cmath>

using namespace audium;
using namespace analysisReference;

// The native beat tracking and onset detection (AnalysisAudioLoader -> Stft
// -> OnsetStrength -> TempoEstimator / BeatTracker, or peak picking) held
// against a synthetic click track and against what Essentia produced for
// the test recordings. Runs in every build, with or without Essentia.

namespace {

// A click track: short decaying noise bursts on every beat, over a quiet
// noise floor so the material is not digital silence between them.
juce::File writeClickTrack(double sampleRate, double bpm, double seconds)
{
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("AudionautTests")
                          .getChildFile("click-track-" + juce::String((int) sampleRate) + ".wav");
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

    juce::Random random(11);
    const auto numSamples = (int) (sampleRate * seconds);
    juce::AudioBuffer<float> buffer(1, numSamples);
    auto* out = buffer.getWritePointer(0);

    const auto beatSamples = 60.0 / bpm * sampleRate;
    const auto clickSamples = (int) (0.01 * sampleRate);
    for (int i = 0; i < numSamples; ++i)
        out[i] = 0.002f * (random.nextFloat() * 2.0f - 1.0f);
    for (double beat = 0.0; beat < numSamples; beat += beatSamples)
    {
        const auto start = (int) std::lround(beat);
        for (int i = 0; i < clickSamples && start + i < numSamples; ++i)
            out[start + i] += 0.8f * (random.nextFloat() * 2.0f - 1.0f) * (1.0f - (float) i / (float) clickSamples);
    }
    writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
    return file;
}

std::vector<float> beatGrid(double bpm, double seconds)
{
    std::vector<float> grid;
    for (double t = 0.0; t < seconds; t += 60.0 / bpm)
        grid.push_back((float) t);
    return grid;
}

} // namespace

SCENARIO("The native beat tracking follows a click track", "[engine][analysis][beats][native]")
{
    for (auto sampleRate : { 44100.0, 48000.0 })
    {
        DYNAMIC_SECTION("at " << (int) sampleRate << " Hz")
        {
            const auto file = writeClickTrack(sampleRate, 120.0, 30.0);
            const auto grid = beatGrid(120.0, 30.0);

            BeatSegmenter beatSegmenter;
            const auto rhythm = beatSegmenter.analyzeNative(file, BeatSegmenter::nativeDefaults());

            OnsetSegmenter onsetSegmenter;
            const auto onsets = onsetSegmenter.analyzeNative(file, OnsetSegmenter::nativeDefaults());

            const auto beatF = fMeasure(grid, rhythm.beats, beatToleranceSeconds);
            const auto onsetRecall = agreement(grid, onsets, onsetToleranceSeconds);

            INFO("beats: " << describe(rhythm.beats));
            INFO("onsets: " << describe(onsets));

            CHECK(beatF >= 0.95f);
            CHECK(rhythm.bpm == Catch::Approx(120.0f).margin(2.0f));
            CHECK(onsetRecall >= 0.9f);

            file.deleteFile();
        }
    }

    GIVEN("a missing file")
    {
        const auto missing = juce::File("/non/existent/audio/file.wav");
        BeatSegmenter beatSegmenter;
        OnsetSegmenter onsetSegmenter;

        THEN("nothing comes back")
        {
            CHECK(beatSegmenter.analyzeNative(missing, BeatSegmenter::nativeDefaults()).beats.empty());
            CHECK(onsetSegmenter.analyzeNative(missing, OnsetSegmenter::nativeDefaults()).empty());
        }
    }

    GIVEN("an abort flag already set")
    {
        const auto file = writeClickTrack(44100.0, 120.0, 2.0);
        std::atomic<bool> abort { true };
        BeatSegmenter beatSegmenter;
        OnsetSegmenter onsetSegmenter;

        THEN("the analyses are abandoned")
        {
            CHECK(beatSegmenter.analyzeNative(file, BeatSegmenter::nativeDefaults(), &abort).beats.empty());
            CHECK(onsetSegmenter.analyzeNative(file, OnsetSegmenter::nativeDefaults(), &abort).empty());
        }

        file.deleteFile();
    }
}

// Hidden: for each recording, the tempo the estimator picks under various
// priors, and the strongest autocorrelation lags it chooses from. Run with
// "[tempo-sweep]" and the compact reporter.
SCENARIO("Native tempo prior sweep", "[.][tempo-sweep]")
{
    for (const auto& name : recordings)
    {
        Fixture fixture;
        if (load(name, fixture))
            continue;

        AnalysisAudioLoader loader;
        AnalysisAudioLoader::Parameters loaderParams;
        loaderParams.targetSampleRate = 22050.0;
        const auto audio = loader.load(fixture.audioFile, loaderParams);
        REQUIRE(audio);

        const Stft stft;
        const auto fps = audio->sampleRate / (double) stft.getParameters().hopSize;
        OnsetStrength onsetStrength(OnsetStrength::Parameters(), stft.numBins(), audio->sampleRate);
        std::vector<float> envelope;
        stft.forEachPowerFrame(audio->samples, [&](int, const float* power) { envelope.push_back(onsetStrength.next(power)); });

        std::string report = name + " reference " + std::to_string(fixture.bpm) + "\n";

        // the autocorrelation peaks within the tempo range
        const auto n = (int) envelope.size();
        std::vector<double> ac(385, 0.0);
        for (int lag = 0; lag < 385 && lag < n; ++lag)
            for (int t = 0; t + lag < n; ++t)
                ac[(size_t) lag] += (double) envelope[(size_t) t] * envelope[(size_t) (t + lag)];
        std::vector<std::pair<double, int>> peaks;
        for (int lag = 2; lag < 384; ++lag)
        {
            const auto bpm = 60.0 * fps / lag;
            if (bpm >= 40.0 && bpm <= 208.0 && ac[(size_t) lag] > ac[(size_t) lag - 1] && ac[(size_t) lag] >= ac[(size_t) lag + 1])
                peaks.emplace_back(ac[(size_t) lag] / ac[0], lag);
        }
        std::sort(peaks.rbegin(), peaks.rend());
        report += "  ac peaks:";
        for (size_t i = 0; i < std::min<size_t>(5, peaks.size()); ++i)
            report += " " + std::to_string((int) std::lround(60.0 * fps / peaks[i].second)) + "bpm=" + std::to_string(peaks[i].first).substr(0, 5);
        report += "\n";

        const TempoEstimator estimator;
        for (auto start : { 100.0f, 110.0f, 120.0f })
        {
            report += "  start " + std::to_string((int) start) + ":";
            for (auto spread : { 0.5f, 0.7f, 1.0f })
            {
                TempoEstimator::Parameters params;
                params.startTempo = start;
                params.tempoSpreadOctaves = spread;
                report += " s" + std::to_string(spread).substr(0, 3) + "=" + std::to_string((int) std::lround(estimator.estimate(envelope, fps, params)));
            }
            report += "\n";
        }
        WARN(report);
    }
}

SCENARIO("The native beat tracking and onsets agree with the recorded reference", "[engine][analysis][reference][native]")
{
    for (const auto& name : recordings)
    {
        DYNAMIC_SECTION(name)
        {
            Fixture fixture;
            if (auto reason = load(name, fixture))
                SKIP(*reason);

            BeatSegmenter beatSegmenter;
            auto started = std::chrono::steady_clock::now();
            const auto rhythm = beatSegmenter.analyzeNative(fixture.audioFile, BeatSegmenter::nativeDefaults());
            const auto beatSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

            OnsetSegmenter onsetSegmenter;
            started = std::chrono::steady_clock::now();
            const auto onsets = onsetSegmenter.analyzeNative(fixture.audioFile, OnsetSegmenter::nativeDefaults());
            const auto onsetSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

            const auto beatF = fMeasure(fixture.beats, rhythm.beats, beatToleranceSeconds);
            const auto onsetF = fMeasure(fixture.onsets, onsets, onsetToleranceSeconds);

            WARN("native rhythm - " << name << " (" << fixture.duration << " s, beats in " << beatSeconds
                 << " s, onsets in " << onsetSeconds << " s)\n"
                 << "  beats: " << fixture.beats.size() << " reference / " << rhythm.beats.size()
                 << " candidate, F = " << beatF << " at " << beatToleranceSeconds << " s\n"
                 << "  bpm: " << fixture.bpm << " reference / " << rhythm.bpm << " candidate\n"
                 << "  onsets: " << fixture.onsets.size() << " reference / " << onsets.size()
                 << " candidate, F = " << onsetF << " at " << onsetToleranceSeconds << " s");

            CHECK_FALSE(rhythm.beats.empty());
            CHECK(std::is_sorted(rhythm.beats.begin(), rhythm.beats.end()));
            CHECK(beatF >= minimumBeatFMeasure);
            CHECK(rhythm.bpm > 0.0f);
            CHECK(tempoAgrees(rhythm.bpm, fixture.bpm));

            CHECK_FALSE(onsets.empty());
            CHECK(onsetF >= minimumOnsetFMeasure);
        }
    }
}
