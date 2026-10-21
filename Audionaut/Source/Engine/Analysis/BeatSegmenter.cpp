//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "BeatSegmenter.h"

#include "AnalysisAudioLoader.h"
#include "Stft.h"
#include "OnsetStrength.h"
#include "TempoEstimator.h"
#include "BeatTracker.h"

namespace audium {

BeatSegmenter::Result BeatSegmenter::analyze(const juce::File& audioFile)
{
    return analyze(audioFile, Parameters());
}

BeatSegmenter::Result BeatSegmenter::analyze(const juce::File& audioFile,
                                             const Parameters& params,
                                             const std::atomic<bool>* shouldAbort)
{
    AnalysisAudioLoader loader;
    AnalysisAudioLoader::Parameters loaderParams;
    loaderParams.targetSampleRate = params.sampleRate;
    const auto audio = loader.load(audioFile, loaderParams, shouldAbort);
    if (! audio)
        return {};

    const Stft stft;
    const auto framesPerSecond = audio->sampleRate / (double) stft.getParameters().hopSize;

    // Two envelopes from the one pass. The tempo is the periodicity of all
    // the onsets, and the dB envelope is what the estimator's prior is
    // calibrated on; the beats' phase is where the loud hits are, so the
    // tracker gets the flux of the magnitudes. On the dB envelope it locks
    // onto a hi-hat pattern a sixteenth off the kick. Held against
    // Essentia's beats on six recordings, the magnitude flux is the one
    // envelope that carries all of them; see the hidden beat-phase check in
    // Catch2Tests/Tests/BeatPhaseCheckTests.cpp.
    OnsetStrength plain(OnsetStrength::Parameters(), stft.numBins(), audio->sampleRate);
    OnsetStrength::Parameters beatParams;
    beatParams.linearFlux = params.linearBeatFlux;
    OnsetStrength emphasised(beatParams, stft.numBins(), audio->sampleRate);

    const auto numFrames = (size_t) stft.numFrames(audio->samples.size());
    std::vector<float> tempoEnvelope, beatEnvelope;
    tempoEnvelope.reserve(numFrames);
    beatEnvelope.reserve(numFrames);
    const auto completed = stft.forEachPowerFrame(audio->samples,
                                                  [&](int, const float* power)
                                                  {
                                                      tempoEnvelope.push_back(plain.next(power));
                                                      beatEnvelope.push_back(emphasised.next(power));
                                                  },
                                                  shouldAbort);
    if (! completed)
        return {};

    TempoEstimator::Parameters tempoParams;
    tempoParams.minTempo = (float) params.minTempo;
    tempoParams.maxTempo = (float) params.maxTempo;
    const TempoEstimator tempoEstimator;
    const auto tempo = tempoEstimator.estimate(tempoEnvelope, framesPerSecond, tempoParams);
    if (tempo <= 0.0f)
        return {};

    const BeatTracker tracker;
    const auto tracked = tracker.track(beatEnvelope, framesPerSecond, tempo, BeatTracker::Parameters(), shouldAbort);

    Result result;
    result.beats.reserve(tracked.beatFrames.size());
    for (auto frame : tracked.beatFrames)
        result.beats.push_back((float) ((double) frame / framesPerSecond));
    result.bpm = tracked.bpm > 0.0f ? tracked.bpm : tempo;
    return result;
}

} // namespace audium
