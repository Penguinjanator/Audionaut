/*
  ==============================================================================

    AnalysisProvider.cpp
    Created: 28 Jun 2026 3:53:22pm
    Author:  Klaus Voltmer

  ==============================================================================
*/

#include "AnalysisProvider.h"
#include "Engine/Resource/AudioResource.h"
#include "Engine/Provider/TempoProvider.h"

#include <algorithm>
#include <cmath>


namespace audium {

void AnalysisProvider::analyzeSegments(AudioTrack& audioTrack)
{
    analyzeTrack(audioTrack, AnalysisType::Segments);
}

void AnalysisProvider::analyzeOnsets(AudioTrack& audioTrack)
{
    analyzeTrack(audioTrack, AnalysisType::Onsets);
}

void AnalysisProvider::analyzeBeats(AudioTrack& audioTrack)
{
    analyzeTrack(audioTrack, AnalysisType::Beats);
}

void AnalysisProvider::analyzeTrack(AudioTrack& audioTrack, AnalysisType analysisType)
{
    auto resources = audioTrack.getAudioResources();
    if (resources.empty())
    {
        std::cout << "AnalysisProvider::analyzeTrack() - track has no audio resources to analyse." << std::endl;
        return;
    }

    for (const auto& resource : resources)
    {
        if (resource == nullptr)
            continue;

        analyzeFile(juce::File(resource->getFullPathName()), analysisType);
    }
}

std::vector<float> AnalysisProvider::analyzeFile(const juce::File& audioFile,
                                                 AnalysisType analysisType,
                                                 const std::atomic<bool>* shouldAbort)
{
    auto results = analyzeFile(audioFile, std::vector<AnalysisType> { analysisType }, shouldAbort);
    const auto it = results.find(analysisType);
    return it != results.end() ? std::move(it->second) : std::vector<float>{};
}

std::unordered_map<AnalysisType, std::vector<float>>
    AnalysisProvider::analyzeFile(const juce::File& audioFile,
                                  const std::vector<AnalysisType>& analysisTypes,
                                  const std::atomic<bool>* shouldAbort)
{
    std::unordered_map<AnalysisType, std::vector<float>> results;

    // Reuse cached results when the file is unchanged; what is left runs in
    // one pass and is cached for next time.
    const auto collectMissing = [&]
    {
        std::vector<AnalysisType> missing;
        for (auto analysisType : analysisTypes)
        {
            if (results.count(analysisType) > 0 || std::find(missing.begin(), missing.end(), analysisType) != missing.end())
                continue;
            if (auto cached = analysisCache->get(audioFile, analysisType))
                results[analysisType] = std::move(*cached);
            else
                missing.push_back(analysisType);
        }
        return missing;
    };

    auto missing = collectMissing();
    if (missing.empty())
    {
        std::cout << "AnalysisProvider::analyzeFile() - cache hit for "
                  << audioFile.getFileName() << std::endl;
        return results;
    }

    AnalysisRun::Result run;
    {
        // Serialise the actual run; a concurrent caller may have computed
        // some of it while we waited, so re-check the cache.
        std::lock_guard<std::mutex> lock(segmenterMutex);
        missing = collectMissing();
        if (! missing.empty())
            run = analysisRun.analyze(audioFile, missing, shouldAbort);
    }

    // An abandoned analysis (see AnalysisWorker's destructor) has no result
    // worth caching or announcing.
    if (shouldAbort != nullptr && shouldAbort->load())
    {
        std::cout << "AnalysisProvider::analyzeFile() - abandoned analysis of "
                  << audioFile.getFileName() << std::endl;
        return results;
    }

    // Only cache successful analyses so failures are retried next time.
    auto changed = false;
    const auto store = [&](AnalysisType analysisType, const std::vector<float>& segments, float bpm)
    {
        if (! segments.empty())
        {
            analysisCache->put(audioFile, analysisType, segments, bpm);
            results[analysisType] = segments;
            changed = true;
        }
        std::cout << "AnalysisProvider::analyzeFile() - " << segments.size()
                  << " " << analysisTypeToString(analysisType) << " for " << audioFile.getFileName() << std::endl;
    };
    if (run.segments)
        store(AnalysisType::Segments, *run.segments, 0.0f);
    if (run.beats)
        store(AnalysisType::Beats, run.beats->beats, run.beats->bpm);
    if (run.onsets)
        store(AnalysisType::Onsets, *run.onsets, 0.0f);

    // Notify listeners (e.g. AudioClipView) that new results are ready to
    // display. Safe to call from any thread - AnalysisWorker runs this on a
    // background thread and ChangeBroadcaster dispatches asynchronously on
    // the message thread.
    if (changed)
        sendChangeMessage();

    // Results are held (and persisted) by the cache - the UI queries them
    // back via getSegments()/getBpm(), so nothing further to store here.
    return results;
}

bool AnalysisProvider::matchesGrid(double projectTempoBpm,
                                   float detectedBpm,
                                   const std::vector<float>& beatSeconds,
                                   double clipStartClocks,
                                   juce::Range<double> playedRegionSeconds)
{
    if (projectTempoBpm <= 0.0 || detectedBpm <= 0.0f)
        return false;

    if (std::abs((double) detectedBpm - projectTempoBpm)
            > projectTempoBpm * tempoMatchTolerance)
        return false;

    // Map each played beat onto the timeline and measure its distance to the
    // nearest grid beat. The mean (rather than the maximum) deviation decides,
    // so a single jittery beat estimate cannot spoil an otherwise aligned clip.
    auto totalDeviationBeats = 0.0;
    auto beatCount = 0;

    for (auto beatTime : beatSeconds)
    {
        if (beatTime < playedRegionSeconds.getStart()
            || beatTime > playedRegionSeconds.getEnd())
            continue;

        const auto timelineClocks = clipStartClocks
            + TempoProvider::secondsToClocks(projectTempoBpm,
                                             beatTime - playedRegionSeconds.getStart());
        const auto timelineBeats = TempoProvider::clocksToBeats(timelineClocks);

        totalDeviationBeats += std::abs(timelineBeats - std::round(timelineBeats));
        ++beatCount;
    }

    if (beatCount == 0)
        return false;

    return totalDeviationBeats / beatCount <= gridMatchToleranceBeats;
}

const std::vector<AnalysisType>& AnalysisProvider::getMergeAnalysisTypes()
{
    // Stream order matters: EventMerger expects the segmentation stream first.
    static const std::vector<AnalysisType> types {
        AnalysisType::Segments,
        AnalysisType::Beats
    };

    return types;
}

std::vector<EventMerger::EventStream>
    AnalysisProvider::makeMergeStreams(const std::vector<float>& segments,
                                       const std::vector<float>& beats)
{
    std::vector<EventMerger::EventStream> streams;

    const auto add = [&streams] (const char* label,
                                 EventMerger::Kind kind,
                                 const std::vector<float>& times)
    {
        // An analysis that found nothing contributes no stream, so the labels
        // stay meaningful and the column order stays predictable.
        if (times.empty())
            return;

        EventMerger::EventStream stream;
        stream.label = label;
        stream.kind = kind;
        stream.times = times;

        streams.push_back(std::move(stream));
    };

    // The structural boundaries are the rare events, so they carry the widest
    // kernel and dominate the merge. Beats are the dense event stream they are
    // aligned to.
    add("segments", EventMerger::Kind::Segmentation, segments);
    add("beats", EventMerger::Kind::Beat, beats);

    return streams;
}

std::vector<AnalysisType>
    AnalysisProvider::findMissingMergeAnalyses(const juce::File& audioFile) const
{
    std::vector<AnalysisType> missing;

    for (auto analysisType : getMergeAnalysisTypes())
        if (! analysisCache->get(audioFile, analysisType).has_value())
            missing.push_back(analysisType);

    return missing;
}

EventMerger::Result AnalysisProvider::mergeCachedAnalyses(const juce::File& audioFile,
                                                          float durationSeconds,
                                                          const EventMerger::Parameters& params) const
{
    // Cache-only by design: running the analyses here would block whichever
    // thread called us, and AnalysisWorker has normally cached them already.
    // A caller wanting to explain the gap asks findMissingMergeAnalyses().
    auto segments = analysisCache->get(audioFile, AnalysisType::Segments);
    auto beats    = analysisCache->get(audioFile, AnalysisType::Beats);

    if (! segments || ! beats)
        return {};

    auto streams = makeMergeStreams(*segments, *beats);

    if (streams.empty())
        return {};

    EventMerger merger;
    return merger.merge(streams, durationSeconds, params);
}

EventMerger::Result AnalysisProvider::mergeCachedAnalyses(const juce::File& audioFile,
                                                          float durationSeconds) const
{
    return mergeCachedAnalyses(audioFile, durationSeconds, EventMerger::Parameters());
}

void AnalysisProvider::setMergePreview(const std::string& audioFilePath,
                                       int trackId,
                                       int playlistItemId,
                                       std::vector<float> boundaries,
                                       double measures)
{
    mergePreviewFilePath = audioFilePath;
    mergePreviewTrackId = trackId;
    mergePreviewPlaylistItemId = playlistItemId;
    mergePreviewBoundaries = std::move(boundaries);
    mergePreviewMeasures = measures;
    sendChangeMessage();
}

void AnalysisProvider::clearMergePreview()
{
    if (mergePreviewFilePath.empty() && mergePreviewBoundaries.empty())
        return;

    mergePreviewFilePath.clear();
    mergePreviewTrackId = -1;
    mergePreviewPlaylistItemId = -1;
    mergePreviewBoundaries.clear();
    mergePreviewMeasures = 0.0;
    sendChangeMessage();
}

std::vector<float> AnalysisProvider::getMergePreview(const juce::File& audioFile,
                                                     int trackId,
                                                     int playlistItemId) const
{
    if (mergePreviewFilePath != audioFile.getFullPathName().toStdString())
        return {};

    // A preview without a clip applies to the whole file and shows on all of
    // its clips; one with a clip shows on that clip alone.
    if (mergePreviewPlaylistItemId >= 0
        && (mergePreviewTrackId != trackId || mergePreviewPlaylistItemId != playlistItemId))
        return {};

    return mergePreviewBoundaries;
}

std::vector<float> AnalysisProvider::getSegments(AnalysisType analysisType,
                                                 const juce::File& audioFile) const
{
    return analysisCache->get(audioFile, analysisType).value_or(std::vector<float>{});
}

std::unordered_map<std::string, std::vector<float>>
    AnalysisProvider::getSegments(AnalysisType analysisType) const
{
    return analysisCache->getAll(analysisType);
}

float AnalysisProvider::getBpm(AnalysisType analysisType, const juce::File& audioFile) const
{
    return analysisCache->getBpm(audioFile, analysisType).value_or(0.0f);
}

} // namespace audium
