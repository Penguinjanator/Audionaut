//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "AnalysisRun.h"

#include "AnalysisAudioLoader.h"
#include "Stft.h"
#include "Mfcc.h"
#include "BicSegmenter.h"
#include "OnsetStrength.h"
#include "TempoEstimator.h"
#include "BeatTracker.h"

#include <algorithm>

namespace audium {

AnalysisRun::Result AnalysisRun::analyze(const juce::File& audioFile,
                                         const std::vector<AnalysisType>& types,
                                         const std::atomic<bool>* shouldAbort) const
{
    return analyze(audioFile, types, Parameters(), shouldAbort);
}

AnalysisRun::Result AnalysisRun::analyze(const juce::File& audioFile,
                                         const std::vector<AnalysisType>& types,
                                         const Parameters& params,
                                         const std::atomic<bool>* shouldAbort) const
{
    const auto wants = [&types](AnalysisType type)
    {
        return std::find(types.begin(), types.end(), type) != types.end();
    };
    const auto wantSegments = wants(AnalysisType::Segments);
    const auto wantBeats = wants(AnalysisType::Beats);
    const auto wantOnsets = wants(AnalysisType::Onsets);

    Result result;
    if (! wantSegments && ! wantBeats && ! wantOnsets)
        return result;

    jassert(! wantBeats || params.beats.sampleRate == params.segmentation.sampleRate);
    jassert(! wantOnsets || params.onsets.sampleRate == params.segmentation.sampleRate);

    AnalysisAudioLoader loader;
    AnalysisAudioLoader::Parameters loaderParams;
    loaderParams.targetSampleRate = params.segmentation.sampleRate;
    const auto audio = loader.load(audioFile, loaderParams, shouldAbort);
    if (! audio)
        return result;

    Stft::Parameters stftParams;
    stftParams.frameSize = params.segmentation.frameSize;
    stftParams.hopSize = params.segmentation.hopSize;
    const Stft stft(stftParams);
    const auto numFrames = (size_t) stft.numFrames(audio->samples.size());
    const auto framesPerSecond = audio->sampleRate / (double) stftParams.hopSize;

    // the per-frame consumers, each only when asked for
    std::optional<Mfcc> mfcc;
    std::vector<std::vector<float>> features;
    if (wantSegments)
    {
        mfcc.emplace(Mfcc::Parameters(), stft.numBins(), audio->sampleRate);
        features.assign(numFrames, std::vector<float>((size_t) mfcc->getNumCoefficients()));
    }

    std::optional<OnsetStrength> onsetStrength;
    std::vector<float> dbFlux, magnitudeFlux;
    if (wantBeats || wantOnsets)
    {
        onsetStrength.emplace(OnsetStrength::Parameters(), stft.numBins(), audio->sampleRate);
        dbFlux.reserve(numFrames);
        if (wantBeats)
            magnitudeFlux.reserve(numFrames);
    }

    const auto completed = stft.forEachPowerFrame(audio->samples,
                                                  [&](int frame, const float* power)
                                                  {
                                                      if (mfcc)
                                                          mfcc->compute(power, features[(size_t) frame].data());
                                                      if (onsetStrength)
                                                      {
                                                          const auto flux = onsetStrength->nextBoth(power);
                                                          dbFlux.push_back(flux.db);
                                                          if (wantBeats)
                                                              magnitudeFlux.push_back(flux.magnitude);
                                                      }
                                                  },
                                                  shouldAbort);
    if (! completed)
        return result;

    const auto toSeconds = [framesPerSecond](int frame) { return (float) ((double) frame / framesPerSecond); };

    if (wantSegments)
    {
        BicSegmenter::Parameters bicParams;
        bicParams.size1 = params.segmentation.size1;
        bicParams.inc1 = params.segmentation.inc1;
        bicParams.size2 = params.segmentation.size2;
        bicParams.inc2 = params.segmentation.inc2;
        bicParams.minLength = params.segmentation.minimumSegmentLength;
        bicParams.complexityPenaltyWeight = params.segmentation.complexityPenaltyWeight;
        bicParams.correctSmallSampleBias = params.segmentation.correctSmallSampleBias;

        const BicSegmenter segmenter;
        const auto boundaries = segmenter.segment(features, bicParams, shouldAbort);
        if (shouldAbort != nullptr && shouldAbort->load(std::memory_order_relaxed))
            return {};

        std::vector<float> segments;
        segments.reserve(boundaries.size());
        for (auto boundary : boundaries)
            segments.push_back(toSeconds(boundary));
        result.segments = std::move(segments);
    }

    if (wantBeats)
    {
        // The tempo is the periodicity of all the onsets, and the dB
        // envelope is what the estimator's prior is calibrated on; the beats'
        // phase is where the loud hits are, so the tracker gets the magnitude
        // flux. Held against Essentia's beats on six recordings, that is the
        // one envelope that carries all of them; see the hidden beat-phase
        // check in Catch2Tests/Tests/BeatPhaseCheckTests.cpp.
        TempoEstimator::Parameters tempoParams;
        tempoParams.minTempo = (float) params.beats.minTempo;
        tempoParams.maxTempo = (float) params.beats.maxTempo;
        const TempoEstimator tempoEstimator;
        const auto tempo = tempoEstimator.estimate(dbFlux, framesPerSecond, tempoParams);

        BeatSegmenter::Result beats;
        if (tempo > 0.0f)
        {
            const BeatTracker tracker;
            const auto tracked = tracker.track(params.beats.linearBeatFlux ? magnitudeFlux : dbFlux,
                                               framesPerSecond, tempo, BeatTracker::Parameters(), shouldAbort);
            if (shouldAbort != nullptr && shouldAbort->load(std::memory_order_relaxed))
                return {};

            beats.beats.reserve(tracked.beatFrames.size());
            for (auto frame : tracked.beatFrames)
                beats.beats.push_back(toSeconds(frame));
            beats.bpm = tracked.bpm > 0.0f ? tracked.bpm : tempo;
        }
        result.beats = std::move(beats);
    }

    if (wantOnsets)
    {
        std::vector<float> onsets;
        for (auto frame : OnsetStrength::pickPeaks(dbFlux, framesPerSecond))
            onsets.push_back(toSeconds(frame));
        result.onsets = std::move(onsets);
    }

    return result;
}

} // namespace audium
