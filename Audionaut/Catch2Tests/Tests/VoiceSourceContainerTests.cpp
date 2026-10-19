#include <catch2/catch_test_macros.hpp>

#include "Engine/Factory/AudiumFactory.h"
#include "Engine/Project/ProjectSerializer.h"
#include "Engine/Group/AudioTrackContainer.h"
#include "Engine/Group/AudioTrack.h"
#include "Engine/PlayList/PlayListContainer.h"
#include "Engine/PlayList/PlayListItem.h"
#include "Engine/PlayList/PlayListScheduler.h"
#include "Engine/AudioSources/VoiceSourceContainer.h"
#include "Engine/AudioSources/VoiceSource.h"
#include "Engine/Playback/AudioBusInterface.h"
#include "TestEngine.h"

using namespace audium;

static const auto voiceTestFilesDirectory = String(CURRENT_SOURCE_DIR) + String("/TestFiles/");

SCENARIO("voice sources reach the audio thread only through committed snapshots", "[engine][voice][snapshot]")
{
    TestEngine engine;
    auto tracks = engine->getAudioTrackContainer();
    auto scheduler = engine->getPlayListScheduler();

    auto audioFile = File(voiceTestFilesDirectory + "120-funk-1-sec.wav");
    REQUIRE(audioFile.existsAsFile());

    GIVEN("a clip whose voice source has been committed and pulled") {
        engine->getProjectSerializer()->createNewProject();
        REQUIRE(tracks->addAudioFiles({ audioFile.getFullPathName() }, 0.0, nullptr, false));

        std::shared_ptr<AudioTrack> clipTrack;
        for (auto i = 0; i < tracks->getNumItems(); ++i)
            if (tracks->getAudioTrack(i)->getPlayListContainer()->playListItems.size() > 0)
                clipTrack = tracks->getAudioTrack(i);
        REQUIRE(clipTrack != nullptr);

        auto container = clipTrack->getVoiceSourceContainer();
        auto item = clipTrack->getPlayListContainer()->playListItems.getObjects()[0];
        const auto numVoices = item->getVoiceSources().size(); // one per file channel
        REQUIRE(numVoices >= 1);
        auto voice = item->getVoiceSources()[0];
        std::weak_ptr<VoiceSource> watch = voice;

        const auto index = container->getVoiceSourceIndex(voice);
        REQUIRE(index >= 0);

        scheduler->commitPlayListData();   // message thread
        REQUIRE(container->pull());        // audio thread
        REQUIRE(container->getVoiceSourceAtIndex(index) == voice.get());

        WHEN("the clip releases its voice source on the message thread") {
            auto* raw = voice.get();
            item->deinit();                 // removeVoiceSource + drops the item's reference
            REQUIRE(container->getVoiceSourceIndex(voice) == -1);
            REQUIRE(container->getVoiceSourceAtIndex(index) == raw);

            THEN("the audio thread keeps its snapshot until the next commit is pulled") {
                REQUIRE_FALSE(container->pull());

                scheduler->commitPlayListData();
                REQUIRE(container->pull());
                REQUIRE(container->getVoiceSourceAtIndex(index) == nullptr);
            }

            THEN("the object is retired, not destroyed, until the audio thread let go") {
                voice.reset();
                REQUIRE_FALSE(watch.expired());           // held by the container only
                REQUIRE(container->getNumRetired() == numVoices);

                scheduler->commitPlayListData();          // excluded from this snapshot ...
                REQUIRE_FALSE(watch.expired());           // ... but the consumer has not pulled it
                REQUIRE(container->getNumRetired() == numVoices);

                scheduler->commitPlayListData();          // still not pulled: still alive
                REQUIRE_FALSE(watch.expired());

                REQUIRE(container->pull());               // audio thread takes over the snapshot
                scheduler->commitPlayListData();          // next commit may release it
                REQUIRE(watch.expired());
                REQUIRE(container->getNumRetired() == 0);
            }

            THEN("the freed index is not handed out again") {
                REQUIRE(tracks->addAudioFiles({ audioFile.getFullPathName() }, 5.0, nullptr, false));
                for (auto i = 0; i < tracks->getNumItems(); ++i)
                    for (auto& other : tracks->getAudioTrack(i)->getPlayListContainer()->playListItems.getObjects())
                        for (auto& v : other->getVoiceSources())
                            if (v != nullptr && v.get() != raw) {
                                auto* c = tracks->getAudioTrack(i)->getVoiceSourceContainer().get();
                                if (c == container.get())
                                    REQUIRE(c->getVoiceSourceIndex(v) != index);
                            }
            }
        }
    }
}

// Every edit that re-reads a track (undo replay, an applied agent state)
// rebuilds its clips' voice sources and retires the old ones. They are
// released only once the audio thread has pulled a snapshot without them -
// which, before #204, only the playing branch of the callback did.
SCENARIO("retired voice sources are released while the transport is stopped", "[engine][voice][snapshot]")
{
    TestEngine engine;
    auto tracks = engine->getAudioTrackContainer();
    auto scheduler = engine->getPlayListScheduler();

    // the callback below drains the command fifo, like a device would
    engine->getAudioBusInterface()->setPumpsCommandsSynchronously(false);

    auto audioFile = File(voiceTestFilesDirectory + "120-funk-1-sec.wav");
    REQUIRE(audioFile.existsAsFile());

    GIVEN("a clip whose voice source has been committed, with the transport stopped") {
        engine->getProjectSerializer()->createNewProject();
        REQUIRE(tracks->addAudioFiles({ audioFile.getFullPathName() }, 0.0, nullptr, false));

        std::shared_ptr<AudioTrack> clipTrack;
        for (auto i = 0; i < tracks->getNumItems(); ++i)
            if (tracks->getAudioTrack(i)->getPlayListContainer()->playListItems.size() > 0)
                clipTrack = tracks->getAudioTrack(i);
        REQUIRE(clipTrack != nullptr);

        auto container = clipTrack->getVoiceSourceContainer();
        auto item = clipTrack->getPlayListContainer()->playListItems.getObjects()[0];
        const auto numVoices = item->getVoiceSources().size();
        REQUIRE(numVoices >= 1);
        std::weak_ptr<VoiceSource> watch = item->getVoiceSources()[0];

        constexpr int blockSize = 128;
        constexpr double sampleRate = 44100.0;
        scheduler->prepareToPlay(blockSize, sampleRate);

        const auto numChannels = juce::jmax(1, tracks->getNumAudioTrackChannels());
        AudioBuffer<float> in(numChannels, blockSize), out(numChannels, blockSize);
        in.clear();
        juce::dsp::AudioBlock<float> inBlock(in), outBlock(out);
        auto stoppedCallback = [&] {
            outBlock.clear();
            juce::dsp::ProcessContextNonReplacing<float> context(inBlock, outBlock);
            scheduler->process(context, false, 0.0, blockSize);
        };

        scheduler->commitPlayListData();
        stoppedCallback();
        REQUIRE(container->getNumRetired() == 0);

        WHEN("the clip is rebuilt, as every re-read of the track does") {
            item->init();
            REQUIRE_FALSE(watch.expired());
            REQUIRE(container->getNumRetired() == numVoices);

            scheduler->commitPlayListData();      // excluded from this snapshot, not yet pulled
            REQUIRE(container->getNumRetired() == numVoices);

            THEN("a stopped audio callback lets the next commit release the old sources") {
                stoppedCallback();
                scheduler->commitPlayListData();
                REQUIRE(container->getNumRetired() == 0);
                REQUIRE(watch.expired());
                REQUIRE(item->getVoiceSources().size() == numVoices);
            }
        }
    }
}

// Retired sources are released at the next commit - or, since an edit
// retires chains the size of the clips it rebuilt, by a timer as soon as
// the audio thread has pulled the snapshot without them.
SCENARIO("retired voice sources are released by a timer once pulled", "[engine][voice][snapshot][memory]")
{
    TestEngine engine;
    auto tracks = engine->getAudioTrackContainer();
    auto scheduler = engine->getPlayListScheduler();

    auto audioFile = File(voiceTestFilesDirectory + "120-funk-1-sec.wav");
    REQUIRE(audioFile.existsAsFile());

    GIVEN("a clip that was rebuilt and committed") {
        engine->getProjectSerializer()->createNewProject();
        REQUIRE(tracks->addAudioFiles({ audioFile.getFullPathName() }, 0.0, nullptr, false));

        std::shared_ptr<AudioTrack> clipTrack;
        for (auto i = 0; i < tracks->getNumItems(); ++i)
            if (tracks->getAudioTrack(i)->getPlayListContainer()->playListItems.size() > 0)
                clipTrack = tracks->getAudioTrack(i);
        REQUIRE(clipTrack != nullptr);

        auto container = clipTrack->getVoiceSourceContainer();
        auto item = clipTrack->getPlayListContainer()->playListItems.getObjects()[0];
        const auto numVoices = item->getVoiceSources().size();
        std::weak_ptr<VoiceSource> watch = item->getVoiceSources()[0];

        scheduler->commitPlayListData();
        REQUIRE(container->pull());

        item->init();
        scheduler->commitPlayListData();           // the old sources are excluded from this snapshot
        REQUIRE(container->getNumRetired() == numVoices);

        WHEN("the audio thread pulls that snapshot and the timer fires, with no further commit") {
            REQUIRE(container->pull());
            juce::Thread::sleep(80);
            juce::Timer::callPendingTimersSynchronously();

            THEN("the retired sources are gone") {
                REQUIRE(container->getNumRetired() == 0);
                REQUIRE(watch.expired());
            }
        }

        WHEN("the timer fires before the audio thread pulled") {
            juce::Thread::sleep(80);
            juce::Timer::callPendingTimersSynchronously();

            THEN("nothing is released yet") {
                REQUIRE(container->getNumRetired() == numVoices);
                REQUIRE_FALSE(watch.expired());
            }
        }
    }
}
