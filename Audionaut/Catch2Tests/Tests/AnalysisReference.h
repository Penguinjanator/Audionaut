#pragma once

// Shared between the scenarios that hold an analysis against the reference
// fixtures under TestFiles/AnalysisReference (see capture.sh there): the
// fixture loader and the agreement metrics.
//
// The tolerances are deliberately those of a different implementation of the
// same idea, not of a port: beat positions agree to the usual 70 ms window,
// tempo to a few percent or an octave, segment boundaries to a second.

#include <JuceHeader.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace analysisReference {

// mir_eval's beat F-measure window.
constexpr float beatToleranceSeconds = 0.07f;
constexpr float minimumBeatFMeasure = 0.7f;

constexpr float bpmTolerance = 0.04f;

// Where the character of the audio changes is a judgement call that two BIC
// implementations can reasonably make a second apart.
constexpr float segmentToleranceSeconds = 1.0f;
constexpr float minimumSegmentAgreement = 0.6f;
constexpr double maximumSegmentCountRatio = 3.0;

// Onsets are display-only; the peak pickers differ more than the trackers do.
constexpr float onsetToleranceSeconds = 0.05f;
constexpr float minimumOnsetFMeasure = 0.4f;

// The recordings a fixture exists for, shortest first.
inline const std::vector<std::string> recordings = {
    "120-funk-export",
    "TRK-18-epy-jul",
    "_export_TRK-18",
    "epy-oh-yeah-streicher-fix",
};

struct Fixture {
    juce::File audioFile;
    double duration = 0.0;   // seconds, from the audio file itself
    std::vector<float> segments;
    std::vector<float> beats;
    float bpm = 0.0f;
    std::vector<float> onsets;
};

inline double durationInSeconds(const juce::File& audioFile)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(audioFile));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return 0.0;
    return (double) reader->lengthInSamples / reader->sampleRate;
}

// Returns the reason the fixture cannot be used, or nothing on success. The
// long recordings come through Git LFS; a checkout without them holds pointer
// files no reader can open.
inline std::optional<std::string> load(const std::string& name, Fixture& fixture)
{
    const auto testFilesDirectory = juce::String(CURRENT_SOURCE_DIR) + juce::String("/TestFiles/");
    const auto fixtureFile = juce::File(testFilesDirectory + "AnalysisReference/" + name + ".essentia.json");
    if (! fixtureFile.existsAsFile())
        return name + ".essentia.json is absent - see capture.sh to produce it";

    const auto json = nlohmann::json::parse(fixtureFile.loadFileAsString().toStdString());
    fixture.audioFile = juce::File(testFilesDirectory + json["file"].get<std::string>());
    fixture.duration = durationInSeconds(fixture.audioFile);
    if (fixture.duration <= 0.0)
        return fixture.audioFile.getFileName().toStdString() + " is not a readable audio file (Git LFS not fetched?)";

    fixture.segments = json["segments"].get<std::vector<float>>();
    fixture.beats = json["beats"].get<std::vector<float>>();
    fixture.bpm = json["bpm"].get<float>();
    fixture.onsets = json["onsets"].get<std::vector<float>>();
    return std::nullopt;
}

// Greedy one-to-one matching: each reference event claims the nearest unclaimed
// candidate within the tolerance.
inline float fMeasure(const std::vector<float>& reference,
                      const std::vector<float>& candidate,
                      float tolerance)
{
    if (reference.empty() || candidate.empty())
        return 0.0f;

    std::vector<bool> claimed(candidate.size(), false);
    int matches = 0;

    for (auto event : reference)
    {
        auto best = candidate.size();
        auto bestDistance = tolerance;
        for (size_t i = 0; i < candidate.size(); ++i)
        {
            if (claimed[i])
                continue;
            auto distance = std::abs(candidate[i] - event);
            if (distance <= bestDistance)
            {
                bestDistance = distance;
                best = i;
            }
        }
        if (best != candidate.size())
        {
            claimed[best] = true;
            ++matches;
        }
    }

    auto precision = (float) matches / (float) candidate.size();
    auto recall = (float) matches / (float) reference.size();
    if (precision + recall == 0.0f)
        return 0.0f;
    return 2.0f * precision * recall / (precision + recall);
}

// Fraction of events that have some event of the other list within the
// tolerance. Read in both directions, since one side alone grows simply by
// having more events.
inline float agreement(const std::vector<float>& events,
                       const std::vector<float>& others,
                       float tolerance)
{
    if (events.empty())
        return 1.0f;

    int hits = 0;
    for (auto event : events)
        for (auto other : others)
            if (std::abs(other - event) <= tolerance)
            {
                ++hits;
                break;
            }
    return (float) hits / (float) events.size();
}

inline bool withinBpmTolerance(float bpm, float referenceBpm)
{
    return std::abs(bpm - referenceBpm) <= bpmTolerance * referenceBpm;
}

// Octave errors are the beat tracker's classic failure, and the consumers
// (grid snapping, clip tempo) can live with them more easily than with a
// tempo that is simply wrong.
inline bool tempoAgrees(float bpm, float referenceBpm)
{
    return withinBpmTolerance(bpm, referenceBpm)
        || withinBpmTolerance(bpm, 2.0f * referenceBpm)
        || withinBpmTolerance(bpm, 0.5f * referenceBpm);
}

// The boundaries between the leading 0 and the trailing end of the material.
inline std::vector<float> interior(const std::vector<float>& boundaries)
{
    if (boundaries.size() < 2)
        return {};
    return std::vector<float>(boundaries.begin() + 1, boundaries.end() - 1);
}

inline std::string describe(const std::vector<float>& events)
{
    std::string text;
    for (auto event : events)
    {
        if (! text.empty())
            text += ", ";
        text += std::to_string(event);
    }
    return text;
}

// What the segment checks share: the span convention and, where the reference
// has interior boundaries, the mutual agreement. Reported, then checked.
struct SegmentComparison {
    std::vector<float> referenceInterior, candidateInterior;
    float hits = 0.0f, hitsReverse = 0.0f;
    double countRatio = 1.0;

    SegmentComparison(const std::vector<float>& reference, const std::vector<float>& candidate)
        : referenceInterior(interior(reference)), candidateInterior(interior(candidate))
    {
        hits = agreement(referenceInterior, candidateInterior, segmentToleranceSeconds);
        hitsReverse = agreement(candidateInterior, referenceInterior, segmentToleranceSeconds);
        if (! referenceInterior.empty() && ! candidateInterior.empty())
            countRatio = (double) std::max(referenceInterior.size(), candidateInterior.size())
                       / (double) std::min(referenceInterior.size(), candidateInterior.size());
    }

    std::string report(const std::vector<float>& reference, const std::vector<float>& candidate) const
    {
        return "  segments: " + std::to_string(referenceInterior.size()) + " reference / "
             + std::to_string(candidateInterior.size()) + " candidate interior boundaries\n"
             + "    reference within " + std::to_string(segmentToleranceSeconds) + " s of a candidate: "
             + std::to_string((int) (hits * 100.0f)) + " %\n"
             + "    candidate within " + std::to_string(segmentToleranceSeconds) + " s of a reference: "
             + std::to_string((int) (hitsReverse * 100.0f)) + " %\n"
             + "    reference: " + describe(reference) + "\n"
             + "    candidate: " + describe(candidate);
    }
};

} // namespace analysisReference
