//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

// Hidden bench: what one clip's playback chain and its parts cost in live
// heap bytes (malloc statistics, exact). Run with '[memory]'. The numbers
// that matter: a prepared Rubber Band instance is ~5 MB per channel, so a
// RePitch clip must not carry one (StretchAudioSource::Stretchers::onDemand),
// and a clip of a stereo file should cost well under a megabyte.
#include <catch2/catch_test_macros.hpp>
#include <iomanip>

#include "Engine/Factory/AudiumFactory.h"
#include "Engine/Project/ProjectSerializer.h"
#include "Engine/Group/AudioTrackContainer.h"
#include "Engine/Group/AudioTrack.h"
#include "Engine/Group/AudioRegionAdapter.h"
#include "Engine/PlayList/PlayListContainer.h"
#include "Engine/PlayList/PlayListItem.h"
#include "Engine/PlayList/PlayListScheduler.h"
#include "Engine/PlayList/ClipSpeed.h"
#include "Engine/AudioSources/VoiceSourceContainer.h"
#include "Engine/AudioSources/VoiceSource.h"
#include "Engine/AudioSources/ClipTransportSource.h"
#include "Engine/AudioSources/StretchAudioSource.h"
#include "Engine/AudioSources/ClipResamplingSource.h"
#include "Engine/AudioSources/Stretch/RubberBandStretchBackend.h"
#include <rubberband/RubberBandStretcher.h>
#include "Engine/Resource/AudioResourceContainer.h"
#include "Engine/Resource/AudioResource.h"
#include "Engine/Resource/audium_AudioThumbnail.h"
#include "TestEngine.h"

// Exact live-heap numbers come from Darwin's libmalloc; the bench is hidden
// anyway, so the other platforms simply do without it.
#if JUCE_MAC
#include <malloc/malloc.h>

using namespace audium;

static double liveMB()
{
    malloc_statistics_t stats;
    malloc_zone_statistics (nullptr, &stats);
    return (double) stats.size_in_use / (1024.0 * 1024.0);
}

static const auto benchFile = File (String(CURRENT_SOURCE_DIR) + "/TestFiles/120-funk-1-sec.wav");

TEST_CASE("memory per chain part", "[.][bench][memory]")
{
    TestEngine engine;
    auto tracks = engine->getAudioTrackContainer();
    engine->getProjectSerializer()->createNewProject();
    REQUIRE(benchFile.existsAsFile());
    REQUIRE(tracks->addAudioFiles({ benchFile.getFullPathName() }, 0.0, nullptr, false));

    std::shared_ptr<AudioResource> resource;
    for (auto t : tracks->getAudioTracks())
        for (auto& item : t->getPlayListContainer()->getPlayListItems())
            for (auto& r : item->getRegion()->getAudioResources()) if (resource == nullptr) resource = r;
    REQUIRE(resource != nullptr);
    auto* reader = resource->audioFormatReader.get();
    std::cout << "reader: memory-mapped=" << (dynamic_cast<MemoryMappedAudioFormatReader*>(reader) != nullptr)
              << " channels=" << reader->numChannels << " bits=" << reader->bitsPerSample
              << " rate=" << reader->sampleRate << " length=" << reader->lengthInSamples / reader->sampleRate << " s" << std::endl;

    constexpr int n = 16;
    auto measure = [&] (const char* what, auto makeOne) {
        std::vector<std::shared_ptr<void>> keep;
        const auto before = liveMB();
        for (int i = 0; i < n; ++i) keep.push_back (makeOne());
        std::cout << std::fixed << std::setprecision (3) << (liveMB() - before) / n << " MB  " << what << std::endl;
    };
    struct Two { std::shared_ptr<void> a, b; };
    struct Three { std::shared_ptr<void> a, b, c; };
    auto hold2 = [] (std::shared_ptr<void> a, std::shared_ptr<void> b) { return std::shared_ptr<void> (new Two { a, b }, [] (void* p) { delete static_cast<Two*> (p); }); };
    auto hold3 = [] (std::shared_ptr<void> a, std::shared_ptr<void> b, std::shared_ptr<void> c) { return std::shared_ptr<void> (new Three { a, b, c }, [] (void* p) { delete static_cast<Three*> (p); }); };

    measure ("raw RubberBand::RubberBandStretcher 1 ch, realtime + finer, no setMaxProcessSize", [&] {
        return std::make_shared<RubberBand::RubberBandStretcher> (48000, 1,
            RubberBand::RubberBandStretcher::OptionProcessRealTime | RubberBand::RubberBandStretcher::OptionEngineFiner
            | RubberBand::RubberBandStretcher::OptionThreadingNever);
    });
    measure ("raw RubberBand::RubberBandStretcher 1 ch + setMaxProcessSize (window)", [&] {
        auto st = std::make_shared<RubberBand::RubberBandStretcher> (48000, 1,
            RubberBand::RubberBandStretcher::OptionProcessRealTime | RubberBand::RubberBandStretcher::OptionEngineFiner
            | RubberBand::RubberBandStretcher::OptionThreadingNever);
        st->setMaxProcessSize (juce::jmax ((size_t) 1024, st->getSamplesRequired()));
        return st;
    });
    measure ("raw RubberBand::RubberBandStretcher 1 ch, faster engine (R2)", [&] {
        return std::make_shared<RubberBand::RubberBandStretcher> (48000, 1,
            RubberBand::RubberBandStretcher::OptionProcessRealTime | RubberBand::RubberBandStretcher::OptionEngineFaster
            | RubberBand::RubberBandStretcher::OptionThreadingNever);
    });
    measure ("RubberBandStretchBackend 1 ch, prepared 512 @ 48k", [&] {
        auto b = std::make_shared<RubberBandStretchBackend>();
        b->prepare (1, 48000.0, 512, ClipSpeed::minSpeedRatio, ClipSpeed::maxSpeedRatio);
        return b;
    });
    measure ("RubberBandStretchBackend 2 ch, prepared 512 @ 48k", [&] {
        auto b = std::make_shared<RubberBandStretchBackend>();
        b->prepare (2, 48000.0, 512, ClipSpeed::minSpeedRatio, ClipSpeed::maxSpeedRatio);
        return b;
    });
    measure ("StretchAudioSource 1 ch (live backend only), prepared", [&] {
        auto src = std::make_shared<juce::AudioFormatReaderSource> (reader, false);
        auto stretch = std::make_shared<StretchAudioSource> (src.get(), 1);
        stretch->prepareToPlay (512, 48000.0);
        return hold2 (src, stretch);
    });
    measure ("ClipResamplingSource 1 ch at max ratio, prepared", [&] {
        auto src = std::make_shared<juce::AudioFormatReaderSource> (reader, false);
        auto res = std::make_shared<ClipResamplingSource> (src.get(), false, 1);
        res->setMaximumResamplingRatio (reader->sampleRate / 48000.0 * ClipSpeed::maxSpeedRatio);
        res->prepareToPlay (512, 48000.0);
        return hold2 (src, res);
    });
    measure ("ClipTransportSource chain, live lane only, prepared", [&] {
        auto src = std::make_shared<juce::AudioFormatReaderSource> (reader, false);
        auto transport = std::make_shared<ClipTransportSource>();
        transport->setSource (src.get(), 0, nullptr, reader->sampleRate, (int) reader->numChannels);
        transport->prepareToPlay (512, 48000.0);
        return hold2 (src, transport);
    });
    measure ("ClipTransportSource chain + standby lane, prepared", [&] {
        auto src = std::make_shared<juce::AudioFormatReaderSource> (reader, false);
        auto standby = std::make_shared<juce::AudioFormatReaderSource> (reader, false);
        auto transport = std::make_shared<ClipTransportSource>();
        transport->setSource (src.get(), 0, nullptr, reader->sampleRate, (int) reader->numChannels);
        transport->setStandbySource (standby.get());
        transport->prepareToPlay (512, 48000.0);
        return hold3 (src, standby, transport);
    });
    measure ("VoiceSource via the resource container (what a clip gets per resource)", [&] {
        return resource->getContainer().createTransportSourceForAudioResource (resource);
    });
    measure ("AudioThumbnail copy on the memory-mapped reader, 64 samples per point, fully loaded", [&] {
        // built directly, not through the views' per-file registry: this is
        // what every view used to hold
        auto thumbnail = std::make_shared<audium::AudioThumbnail> (64, *engine->getAudioResourceContainer()->getAudioFormatManager(),
                                                                   *engine->getAudioResourceContainer()->getAudioThumbnailCache());
        thumbnail->setReader (resource->audioFormatReader, resource->getUrl().toString (true).hashCode64());
        for (int i = 0; i < 400 && ! thumbnail->isFullyLoaded(); ++i)
            juce::Thread::sleep (25);
        REQUIRE(thumbnail->isFullyLoaded());
        return thumbnail;
    });
    measure ("AudioThumbnail through the resource container's per-file registry", [&] {
        return resource->getContainer().getThumbnailForResource (resource);
    });
}

TEST_CASE("memory per clip chain", "[.][bench][memory]")
{
    TestEngine engine;
    auto tracks = engine->getAudioTrackContainer();
    auto scheduler = engine->getPlayListScheduler();
    engine->getProjectSerializer()->createNewProject();
    REQUIRE(tracks->addAudioFiles({ benchFile.getFullPathName() }, 0.0, nullptr, false));
    scheduler->commitPlayListData();
    scheduler->prepareToPlay(512, 48000.0);

    auto numClips = [&] { int c = 0; for (auto t : tracks->getAudioTracks()) c += t->getPlayListContainer()->getNumItems(); return c; };
    auto numVoices = [&] { size_t c = 0; for (auto t : tracks->getAudioTracks()) for (auto& i : t->getPlayListContainer()->getPlayListItems()) c += i->getVoiceSources().size(); return c; };
    auto release = [&] {
        for (int k = 0; k < 2; ++k) {
            for (auto t : tracks->getAudioTracks()) t->getVoiceSourceContainer()->pull();
            scheduler->commitPlayListData();
        }
    };

    const auto before = liveMB();
    const auto unitsBefore = engine->getUndoManager()->getNumberOfUnitsTakenUpByStoredCommands();
    const auto clipsBefore = numClips();
    AudioRegionAdapter adapter (*tracks);
    // 64 cuts through a one-second file: every 15 ms
    for (int cut = 1; cut <= 64; ++cut)
        adapter.splitRegions (cut * 0.015, audium::seconds);
    const auto beforeRelease = liveMB();
    release();
    const auto after = liveMB();
    const auto added = numClips() - clipsBefore;
    std::cout << std::fixed << std::setprecision (2)
              << "64 splits: " << added << " clips added, " << numVoices() << " voice sources live\n"
              << "  " << (beforeRelease - before) / added << " MB per clip before the retired chains are released\n"
              << "  " << (after - before) / added << " MB per clip after ("
              << (after - before) / (double) numVoices() << " MB per voice source)\n"
              << "  undo history grew by " << (engine->getUndoManager()->getNumberOfUnitsTakenUpByStoredCommands() - unitsBefore) / 64 << " units per split" << std::endl;
}

#endif // JUCE_MAC
