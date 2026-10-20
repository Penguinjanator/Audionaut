#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Engine/Factory/AudiumFactory.h"
#include "Engine/Project/ProjectSerializer.h"
#include "Engine/Project/ProjectFileStore.h"
#include "Engine/Group/AudioTrackContainer.h"
#include "Engine/Resource/AudioResourceContainer.h"
#include "TestEngine.h"

using namespace audium;

static const auto seamTestFilesDirectory = String(CURRENT_SOURCE_DIR) + String("/TestFiles/");

/**
 * The seams a hosted agent session needs: apply a state computed elsewhere as
 * one undoable step, and keep a second engine in the process from taking the
 * live session's temp directory with it when it goes.
 */
SCENARIO("a project state computed elsewhere applies as one undoable step",
         "[engine][hosted][undo]")
{
    TestEngine engine;
    auto store = engine->getProjectFileStore();
    auto container = engine->getAudioTrackContainer();

    auto outProject = File(seamTestFilesDirectory + "Sessions/hosted-seam-test.audium/"
                           + ProjectFileStore::projectFileName);

    GIVEN("a saved project") {
        engine->getProjectSerializer()->createNewProject();
        container->setMasterGain(1.0f);
        REQUIRE(store->save(outProject, nullptr));

        const auto projectWrittenAt = outProject.getLastModificationTime();

        WHEN("a state carrying a different gain is applied") {
            // stands in for the state a scratch engine would hand back
            json afterState;
            engine->getProjectSerializer()->writeToJson(afterState);
            afterState["audium"]["master_gain"] = 0.25;

            REQUIRE(store->applyStateAsUndoableReload(afterState, true, true,
                                                      "Agent: test", nullptr));

            THEN("the graph carries it, the file does not, and one undo reverses it") {
                REQUIRE(container->getMasterGain() == Catch::Approx(0.25f));
                REQUIRE(outProject.getLastModificationTime() == projectWrittenAt);
                REQUIRE(store->wasChangedExternally());
                REQUIRE(engine->getUndoManager()->canUndo());

                REQUIRE(engine->getUndoManager()->undo());
                REQUIRE(container->getMasterGain() == Catch::Approx(1.0f));
                REQUIRE_FALSE(store->wasChangedExternally());
            }
        }

        WHEN("the applied state is the one already loaded") {
            json sameState;
            engine->getProjectSerializer()->writeToJson(sameState);

            THEN("applying it still succeeds and leaves the file alone") {
                REQUIRE(store->applyStateAsUndoableReload(sameState, true, true,
                                                          "Agent: no-op", nullptr));
                REQUIRE(container->getMasterGain() == Catch::Approx(1.0f));
                REQUIRE(outProject.getLastModificationTime() == projectWrittenAt);
            }
        }
    }

    // cleanup ... comment out in case you need to isolate an issue
    outProject.getParentDirectory().deleteRecursively();
}

/**
 * The UI keys its track and channel components on the engine's objects. A
 * state that kept them only needs a refresh (updateAll), one that replaced
 * them needs a rebuild (rebuildAll) - the serializer picks the broadcast from
 * the container's report, so an agent edit that only touches clips no longer
 * rebuilds the whole arrangement.
 */
SCENARIO("applying a state reports whether the track structure was rebuilt",
         "[engine][hosted][structure]")
{
    MessageManager::getInstance();
    MessageManagerLock mmLock(Thread::getCurrentThread());
    auto engine = AudiumFactory::createAudiumEngine();
    auto store = engine->getProjectFileStore();
    auto container = engine->getAudioTrackContainer();

    auto audioFile = File(seamTestFilesDirectory + "120-funk-1-sec.wav");
    REQUIRE(audioFile.existsAsFile());

    auto outProject = File(seamTestFilesDirectory + "Sessions/hosted-structure-test.audium/"
                           + ProjectFileStore::projectFileName);

    GIVEN("a saved project with a clip track") {
        engine->getProjectSerializer()->createNewProject();
        REQUIRE(container->addAudioFiles({ audioFile.getFullPathName() }, 0.0, nullptr, false));
        REQUIRE(store->save(outProject, nullptr));
        const auto numTracks = container->getNumItems();

        json before;
        engine->getProjectSerializer()->writeToJson(before);
        auto& tracksJson = before["audium"]["audio_tracks"];
        REQUIRE(tracksJson.size() == static_cast<size_t>(numTracks));

        WHEN("a state with the same tracks and channels is applied") {
            auto afterState = before;
            afterState["audium"]["master_gain"] = 0.5;
            REQUIRE(store->applyStateAsUndoableReload(afterState, true, true,
                                                      "Agent: gain", nullptr));

            THEN("the tracks were read in place") {
                REQUIRE(container->getNumItems() == numTracks);
                REQUIRE_FALSE(container->didLastReadRebuildStructure());
            }
        }

        WHEN("a state with one track fewer is applied") {
            auto afterState = before;
            afterState["audium"]["audio_tracks"].erase(tracksJson.size() - 1);
            REQUIRE(store->applyStateAsUndoableReload(afterState, true, true,
                                                      "Agent: remove-track", nullptr));

            THEN("the tracks were replaced") {
                REQUIRE(container->getNumItems() == numTracks - 1);
                REQUIRE(container->didLastReadRebuildStructure());
            }
        }

        WHEN("a state with one channel more on a track is applied") {
            auto afterState = before;
            auto& channels = afterState["audium"]["audio_tracks"][tracksJson.size() - 1]["channels"];
            REQUIRE(channels.size() > 0);
            channels.push_back(channels.back());
            REQUIRE(store->applyStateAsUndoableReload(afterState, true, true,
                                                      "Agent: add-channel", nullptr));

            THEN("the structure counts as replaced although no track was") {
                REQUIRE(container->getNumItems() == numTracks);
                REQUIRE(container->didLastReadRebuildStructure());
            }
        }
    }

    // cleanup ... comment out in case you need to isolate an issue
    outProject.getParentDirectory().deleteRecursively();

    engine = nullptr;
    DeletedAtShutdown::deleteAll();
    MessageManager::deleteInstance();
}

SCENARIO("a second engine leaves the session's temp directory alone",
         "[engine][hosted][temp]")
{
    TestEngine engine;
    auto store = engine->getProjectFileStore();
    engine->getProjectSerializer()->createNewProject();

    const auto sessionTemp = store->getTemporaryDirectory();
    REQUIRE(sessionTemp != File());
    REQUIRE(sessionTemp.isDirectory());

    GIVEN("a scratch engine that borrows the session's directories") {
        {
            auto scratch = AudiumFactory::createAudiumEngine();
            auto scratchStore = scratch->getProjectFileStore();
            scratchStore->getProjectDirectories()->borrowFrom(*store->getProjectDirectories());
            REQUIRE(scratchStore->getTemporaryDirectory() == sessionTemp);
            REQUIRE_FALSE(scratchStore->getProjectDirectories()->ownsTemporaryDirectory());

            // not even an explicit reset may take it
            scratch->getAudioResourceContainer()->createTemporaryProjectDirectory(true);
            REQUIRE(scratchStore->getTemporaryDirectory() == sessionTemp);
        }

        THEN("its teardown leaves the directory in place, still the session's") {
            REQUIRE(store->getTemporaryDirectory() == sessionTemp);
            REQUIRE(sessionTemp.isDirectory());
        }
    }

    GIVEN("a second engine of its own") {
        File secondTemp;
        {
            auto second = AudiumFactory::createAudiumEngine();
            second->getProjectSerializer()->createNewProject();
            secondTemp = second->getProjectFileStore()->getTemporaryDirectory();
            REQUIRE(secondTemp != File());
            REQUIRE(secondTemp != sessionTemp);
            REQUIRE(secondTemp.isDirectory());
        }

        THEN("it had a temp directory of its own and took only that with it") {
            REQUIRE_FALSE(secondTemp.exists());
            REQUIRE(store->getTemporaryDirectory() == sessionTemp);
            REQUIRE(sessionTemp.isDirectory());
        }
    }

    GIVEN("the session's temp directory vanished (a purged cache)") {
        REQUIRE(sessionTemp.deleteRecursively());

        WHEN("a scratch engine that borrowed it needs somewhere for an import") {
            auto scratch = AudiumFactory::createAudiumEngine();
            auto scratchStore = scratch->getProjectFileStore();
            scratchStore->getProjectDirectories()->borrowFrom(*store->getProjectDirectories());
            const auto audioDir = scratch->getAudioResourceContainer()->getCurrentAudioFileDirectory();

            THEN("it is the session's directory again, not one of the scratch engine's own") {
                REQUIRE(audioDir.isDirectory());
                REQUIRE(audioDir.isAChildOf(sessionTemp));
                REQUIRE(scratchStore->getTemporaryDirectory() == sessionTemp);
                REQUIRE(store->getTemporaryDirectory() == sessionTemp);
            }
        }

        WHEN("the session itself needs it") {
            const auto audioDir = engine->getAudioResourceContainer()->getCurrentAudioFileDirectory();

            THEN("it comes back at the same path, so relative resource paths still hold") {
                REQUIRE(audioDir.isDirectory());
                REQUIRE(audioDir.isAChildOf(sessionTemp));
                REQUIRE(store->getTemporaryDirectory() == sessionTemp);
            }
        }
    }
}
