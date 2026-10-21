#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "Engine/Analysis/AnalysisAudioLoader.h"
#include "Engine/Analysis/Stft.h"
#include "Engine/Analysis/BeatSegmenter.h"
#include "AnalysisReference.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace audium;

// Hidden diagnostic: whether the beats sit on the hits. The onset flux of an
// audio file is split into a low (30-160 Hz), mid (160-2000 Hz) and high
// (above 5 kHz) band and sampled at a reference grid's beats - and at that
// grid shifted by a sixteenth, for scale - then at the native tracker's
// beats with each of its envelopes. The reference grid is the "segments" of
// a beat entry in an analyze --json output, or a TestFiles/AnalysisReference
// fixture:
//
//   BEAT_PHASE_AUDIO=<file> BEAT_PHASE_A=<json> AudionautTests "[beat-phase]" -r compact

namespace {

std::vector<float> beatsOf(const char* path)
{
    std::ifstream stream(path);
    const auto json = nlohmann::json::parse(stream);
    if (json.contains("beats"))   // a TestFiles/AnalysisReference fixture
        return json["beats"].get<std::vector<float>>();
    const auto& analyses = json["result"]["files"][0]["analyses"];
    for (const auto& key : { "beats", "beat_degara", "beat" })
        if (analyses.contains(key))
            return analyses[key]["segments"].get<std::vector<float>>();
    return {};
}

} // namespace

SCENARIO("Which beat grid sits on the hits", "[.][beat-phase]")
{
    const auto* audioPath = std::getenv("BEAT_PHASE_AUDIO");
    const auto* pathA = std::getenv("BEAT_PHASE_A");
    REQUIRE(audioPath != nullptr);
    REQUIRE(pathA != nullptr);

    AnalysisAudioLoader loader;
    AnalysisAudioLoader::Parameters loaderParams;
    loaderParams.targetSampleRate = 22050.0;
    const auto audio = loader.load(juce::File(audioPath), loaderParams);
    REQUIRE(audio);

    const Stft stft;
    const auto fps = audio->sampleRate / (double) stft.getParameters().hopSize;
    const auto numBins = stft.numBins();
    const auto binHz = audio->sampleRate / (double) ((numBins - 1) * 2);

    std::vector<int> band((size_t) numBins, -1);
    for (int b = 0; b < numBins; ++b)
    {
        const auto hz = b * binHz;
        if (hz >= 30.0 && hz <= 160.0) band[(size_t) b] = 0;
        else if (hz > 160.0 && hz <= 2000.0) band[(size_t) b] = 1;
        else if (hz > 5000.0) band[(size_t) b] = 2;
    }

    std::vector<std::array<double, 3>> flux;
    std::vector<float> previous((size_t) numBins, 0.0f);
    stft.forEachPowerFrame(audio->samples, [&](int, const float* power)
    {
        std::array<double, 3> sums { 0.0, 0.0, 0.0 };
        std::array<int, 3> counts { 0, 0, 0 };
        for (int b = 0; b < numBins; ++b)
        {
            const auto value = std::log1p(1000.0f * std::sqrt(power[b]));
            if (band[(size_t) b] >= 0)
            {
                sums[(size_t) band[(size_t) b]] += std::max(0.0f, value - previous[(size_t) b]);
                ++counts[(size_t) band[(size_t) b]];
            }
            previous[(size_t) b] = value;
        }
        for (size_t k = 0; k < 3; ++k)
            sums[k] = counts[k] > 0 ? sums[k] / counts[k] : 0.0;
        flux.push_back(sums);
    });

    const auto sample = [&](const std::vector<float>& beats, double shift, double from, double to)
    {
        std::array<double, 3> total { 0.0, 0.0, 0.0 };
        int count = 0;
        for (auto beat : beats)
        {
            if (beat < from || beat >= to) continue;
            const auto i = (int) std::lround((beat + shift) * fps);
            if (i < 1 || i + 1 >= (int) flux.size()) continue;
            for (size_t k = 0; k < 3; ++k)
                total[k] += std::max({ flux[(size_t) i - 1][k], flux[(size_t) i][k], flux[(size_t) i + 1][k] });
            ++count;
        }
        std::string line;
        for (size_t k = 0; k < 3; ++k)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%s %.3f  ", k == 0 ? "low" : k == 1 ? "mid" : "high", count > 0 ? total[k] / count : 0.0);
            line += buffer;
        }
        return line + "(" + std::to_string(count) + " beats)";
    };

    const auto a = beatsOf(pathA);
    REQUIRE_FALSE(a.empty());

    // a sixteenth at the grid's own median interval
    const auto sixteenth = [](const std::vector<float>& beats)
    {
        std::vector<float> gaps;
        for (size_t i = 1; i < beats.size(); ++i) gaps.push_back(beats[i] - beats[i - 1]);
        std::sort(gaps.begin(), gaps.end());
        return gaps.empty() ? 0.0f : gaps[gaps.size() / 2] / 4.0f;
    };

    const auto duration = (double) audio->samples.size() / audio->sampleRate;
    const auto from = std::min(60.0, duration * 0.1), to = duration - from;
    std::string report = "onset flux at the beats, " + std::to_string((int) from) + "-" + std::to_string((int) to) + " s, mean of the frame and its neighbours\n";
    {
        const auto q = sixteenth(a);
        report += "  grid A (file) as is:          " + sample(a, 0.0, from, to) + "\n";
        report += "  grid A (file) a 16th earlier: " + sample(a, -q, from, to) + "\n";
        report += "  grid A (file) a 16th later:   " + sample(a, +q, from, to) + "\n";
    }
    for (auto linear : { false, true })
    {
        BeatSegmenter segmenter;
        BeatSegmenter::Parameters params;
        params.linearBeatFlux = linear;
        const auto result = segmenter.analyze(juce::File(audioPath), params);
        const auto f = analysisReference::fMeasure(a, result.beats, 0.07f);
        char head[96];
        std::snprintf(head, sizeof(head), "  native on the %s flux: bpm %.2f, F vs A %.2f, ",
                      linear ? "magnitude" : "dB", (double) result.bpm, (double) f);
        report += head + sample(result.beats, 0.0, from, to) + "\n";
    }
    WARN(report);
}
