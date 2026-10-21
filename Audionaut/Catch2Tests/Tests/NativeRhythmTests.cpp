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
#include <cstdlib>
#include <fstream>
#include <utility>

using namespace audium;
using namespace analysisReference;

// The beat tracking and onset detection (AnalysisAudioLoader -> Stft ->
// OnsetStrength -> TempoEstimator / BeatTracker, or peak picking) held
// against a synthetic click track. AnalysisReferenceTests holds them against
// what Essentia produced for the test recordings.

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

SCENARIO("The beat tracking follows a click track", "[engine][analysis][beats]")
{
    for (auto sampleRate : { 44100.0, 48000.0 })
    {
        DYNAMIC_SECTION("at " << (int) sampleRate << " Hz")
        {
            const auto file = writeClickTrack(sampleRate, 120.0, 30.0);
            const auto grid = beatGrid(120.0, 30.0);

            BeatSegmenter beatSegmenter;
            const auto rhythm = beatSegmenter.analyze(file);

            OnsetSegmenter onsetSegmenter;
            const auto onsets = onsetSegmenter.analyze(file);

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
            CHECK(beatSegmenter.analyze(missing).beats.empty());
            CHECK(onsetSegmenter.analyze(missing).empty());
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
            CHECK(beatSegmenter.analyze(file, BeatSegmenter::Parameters(), &abort).beats.empty());
            CHECK(onsetSegmenter.analyze(file, OnsetSegmenter::Parameters(), &abort).empty());
        }

        file.deleteFile();
    }
}

// Hidden: for each recording, the tempo the estimator picks under various
// priors, and the strongest autocorrelation lags it chooses from. Run with
// "[tempo-sweep]" and the compact reporter.
SCENARIO("Tempo prior sweep", "[.][tempo-sweep]")
{
    // The recordings with a reference tempo, plus any extra audio files named
    // in TEMPO_SWEEP_FILES (colon separated); TEMPO_SWEEP_DUMP_DIR receives
    // each envelope's autocorrelation as JSON for prototyping a tempo rule.
    std::vector<std::pair<std::string, Fixture>> material;
    for (const auto& name : recordings)
    {
        Fixture fixture;
        if (! load(name, fixture))
            material.emplace_back(name, fixture);
    }
    if (const auto* extra = std::getenv("TEMPO_SWEEP_FILES"))
        for (const auto& path : juce::StringArray::fromTokens(juce::String(extra), ":", {}))
        {
            Fixture fixture;
            fixture.audioFile = juce::File(path);
            if (fixture.audioFile.existsAsFile())
                material.emplace_back(fixture.audioFile.getFileNameWithoutExtension().toStdString(), fixture);
        }

    for (const auto& [name, fixture] : material)
    {
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

        if (const auto* dumpDir = std::getenv("TEMPO_SWEEP_DUMP_DIR"))
        {
            nlohmann::json dump;
            dump["name"] = name;
            dump["fps"] = fps;
            dump["referenceBpm"] = fixture.bpm;
            std::vector<double> normalised;
            for (auto value : ac)
                normalised.push_back(ac[0] > 0.0 ? value / ac[0] : 0.0);
            dump["autocorrelation"] = normalised;
            std::ofstream out(juce::File(dumpDir).getChildFile(name + ".ac.json").getFullPathName().toStdString());
            out << dump.dump() << "\n";
        }

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
