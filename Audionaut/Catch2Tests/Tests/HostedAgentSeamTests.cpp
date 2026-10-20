#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Engine/Factory/AudiumFactory.h"
#include "Engine/Project/ProjectSerializer.h"
#include "Engine/Project/ProjectFileStore.h"
#include "Engine/Group/AudioTrackContainer.h"
#include "Engine/Group/AudioTrack.h"
#include "Engine/PlayList/PlayListContainer.h"
#include "Engine/PlayList/PlayListItem.h"
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
    engine->getProjectSerializer()->createNewProject();

    const auto sessionTemp = ProjectFileStore::tempDirectory;
    REQUIRE(sessionTemp != File());
    REQUIRE(sessionTemp.isDirectory());

    GIVEN("a scratch engine that does not own the temp directory") {
        {
            auto scratch = AudiumFactory::createAudiumEngine();
            scratch->getAudioResourceContainer()->setOwnsTemporaryDirectory(false);
            REQUIRE(ProjectFileStore::tempDirectory == sessionTemp);
        }

        THEN("its teardown leaves the directory and the static in place") {
            REQUIRE(ProjectFileStore::tempDirectory == sessionTemp);
            REQUIRE(sessionTemp.isDirectory());
        }
    }

    GIVEN("a second engine that does own it (the default)") {
        {
            auto owning = AudiumFactory::createAudiumEngine();
            REQUIRE(ProjectFileStore::tempDirectory == sessionTemp);
        }

        THEN("its teardown takes the shared directory with it") {
            // documents why the ownership flag has to exist
            REQUIRE(ProjectFileStore::tempDirectory == File());
            REQUIRE_FALSE(sessionTemp.isDirectory());
        }
    }
}

/**
 * An agent reaches the document one verb at a time. The verbs of one task
 * share an undo step (ProjectFileStore::applyAgentState); what the user does
 * in between, a change of task, or a long pause cuts a new one.
 */
SCENARIO("the verbs of one agent task share one undo step",
         "[engine][hosted][undo]")
{
    TestEngine engine;
    auto store = engine->getProjectFileStore();
    auto container = engine->getAudioTrackContainer();
    auto undoManager = engine->getUndoManager();

    engine->getProjectSerializer()->createNewProject();
    container->setMasterGain(1.0f);
    undoManager->clearUndoHistory();

    auto now = juce::Time::getCurrentTime();
    store->setClock([&now] { return now; });

    // what a scratch engine hands back: the live document with one change
    auto stateWithGain = [&] (double gain) {
        json state;
        engine->getProjectSerializer()->writeToJson(state);
        state["audium"]["master_gain"] = gain;
        return state;
    };
    auto numUndoSteps = [&] { return undoManager->getUndoDescriptions().size(); };

    const ProjectFileStore::AgentEditGroup pauses { "session-1", "remove pauses" };

    GIVEN("two verbs of the same task") {
        REQUIRE(store->applyAgentState(stateWithGain(0.5), pauses, nullptr));
        REQUIRE(store->applyAgentState(stateWithGain(0.25), pauses, nullptr));
        REQUIRE(container->getMasterGain() == Catch::Approx(0.25f));

        THEN("they are one step named after the task") {
            REQUIRE(numUndoSteps() == 1);
            REQUIRE(undoManager->getUndoDescription() == "Agent: remove pauses");
            REQUIRE(store->wasChangedExternally());
        }

        THEN("one undo restores the document before the task, redo its latest result") {
            REQUIRE(undoManager->undo());
            REQUIRE(container->getMasterGain() == Catch::Approx(1.0f));
            REQUIRE_FALSE(store->wasChangedExternally());

            REQUIRE(undoManager->redo());
            REQUIRE(container->getMasterGain() == Catch::Approx(0.25f));
            REQUIRE(store->wasChangedExternally());

            AND_THEN("a further verb after the redo still extends the step") {
                REQUIRE(store->applyAgentState(stateWithGain(0.125), pauses, nullptr));
                REQUIRE(numUndoSteps() == 1);
                REQUIRE(undoManager->undo());
                REQUIRE(container->getMasterGain() == Catch::Approx(1.0f));
            }
        }

        WHEN("the agent names another task") {
            REQUIRE(store->applyAgentState(stateWithGain(0.75), { "session-1", "level the intro" }, nullptr));

            THEN("that starts a second step, and each undoes on its own") {
                REQUIRE(numUndoSteps() == 2);
                REQUIRE(undoManager->getUndoDescription() == "Agent: level the intro");
                REQUIRE(undoManager->undo());
                REQUIRE(container->getMasterGain() == Catch::Approx(0.25f));
                REQUIRE(undoManager->undo());
                REQUIRE(container->getMasterGain() == Catch::Approx(1.0f));
            }
        }

        WHEN("the user edits in between, without an undo step of their own") {
            container->setMasterGain(0.9f);
            REQUIRE(store->applyAgentState(stateWithGain(0.3), pauses, nullptr));

            THEN("the next verb starts a new step that undoes back to the user's state") {
                REQUIRE(numUndoSteps() == 2);
                REQUIRE(undoManager->undo());
                REQUIRE(container->getMasterGain() == Catch::Approx(0.9f));
            }
        }

        WHEN("another edit's undo step lands in between") {
            REQUIRE(store->applyStateAsUndoableReload(stateWithGain(0.6), true, false, "Set Master Gain", nullptr));
            REQUIRE(store->applyAgentState(stateWithGain(0.3), pauses, nullptr));

            THEN("the agent's step comes after it, the user's step stays separate") {
                REQUIRE(numUndoSteps() == 3);
                REQUIRE(undoManager->getUndoDescription() == "Agent: remove pauses");
                REQUIRE(undoManager->undo());
                REQUIRE(container->getMasterGain() == Catch::Approx(0.6f));
                REQUIRE(undoManager->getUndoDescription() == "Set Master Gain");
            }
        }

        WHEN("the user undoes the task and the agent carries on") {
            REQUIRE(undoManager->undo());
            REQUIRE(store->applyAgentState(stateWithGain(0.4), pauses, nullptr));

            THEN("the new work is a new step and the undone one is gone") {
                REQUIRE(numUndoSteps() == 1);
                REQUIRE_FALSE(undoManager->canRedo());
                REQUIRE(container->getMasterGain() == Catch::Approx(0.4f));
                REQUIRE(undoManager->undo());
                REQUIRE(container->getMasterGain() == Catch::Approx(1.0f));
            }
        }

        WHEN("the task pauses for longer than the idle limit") {
            now += juce::RelativeTime::minutes(ProjectFileStore::agentBatchIdleMinutes + 1);
            REQUIRE(store->applyAgentState(stateWithGain(0.3), pauses, nullptr));

            THEN("the next verb starts a new step") {
                REQUIRE(numUndoSteps() == 2);
            }
        }

        WHEN("a different agent session names the same task") {
            REQUIRE(store->applyAgentState(stateWithGain(0.3), { "session-2", "remove pauses" }, nullptr));

            THEN("it gets a step of its own") {
                REQUIRE(numUndoSteps() == 2);
            }
        }
    }

    GIVEN("verbs that name no session") {
        REQUIRE(store->applyAgentState(stateWithGain(0.5), {}, nullptr));
        REQUIRE(store->applyAgentState(stateWithGain(0.25), { "", "remove pauses" }, nullptr));

        THEN("each is a step of its own, the unnamed one under a generic name") {
            REQUIRE(numUndoSteps() == 2);
            REQUIRE(undoManager->getUndoDescription() == "Agent: remove pauses");
            REQUIRE(undoManager->undo());
            REQUIRE(undoManager->getUndoDescription() == "Agent edits");
        }
    }

    GIVEN("the user undoes a later step of their own, leaving it to redo") {
        REQUIRE(store->applyAgentState(stateWithGain(0.5), pauses, nullptr));
        REQUIRE(store->applyStateAsUndoableReload(stateWithGain(0.6), true, false, "Set Master Gain", nullptr));
        REQUIRE(undoManager->undo());
        REQUIRE(undoManager->canRedo());
        REQUIRE(store->applyAgentState(stateWithGain(0.3), pauses, nullptr));

        THEN("the agent's next verb is a step of its own, and the redo is gone") {
            REQUIRE(numUndoSteps() == 2);
            REQUIRE_FALSE(undoManager->canRedo());
            REQUIRE(container->getMasterGain() == Catch::Approx(0.3f));
            REQUIRE(undoManager->undo());
            REQUIRE(container->getMasterGain() == Catch::Approx(0.5f));
        }
    }

    GIVEN("a clip the user selects while a verb runs") {
        juce::StringArray names { seamTestFilesDirectory + "120-funk-1-sec.wav" };
        REQUIRE(container->addAudioFiles(names, 0.0, [] (std::string) {}, false));
        undoManager->clearUndoHistory();

        // the import made a track of its own after the empty first one
        auto firstClip = [&] { return container->getAudioTracks().back()->getPlayListContainer()->getPlayListItems().front(); };
        firstClip()->setSelected(false);
        auto staged = stateWithGain(0.5); // what the verb's engine computed: the clip unselected
        firstClip()->setSelected(true);   // the click, after the verb's engine took its copy

        REQUIRE(store->applyAgentState(staged, pauses, nullptr));

        THEN("the applied state keeps the selection, and so does a redo") {
            REQUIRE(firstClip()->isSelected());
            REQUIRE(container->getMasterGain() == Catch::Approx(0.5f));
            REQUIRE(undoManager->undo());
            REQUIRE(undoManager->redo());
            REQUIRE(firstClip()->isSelected());
        }
    }

    GIVEN("a step of the user's that carries the task's name and leaves the same document") {
        REQUIRE(store->applyAgentState(stateWithGain(0.5), pauses, nullptr));
        REQUIRE(undoManager->undo());
        REQUIRE(store->applyStateAsUndoableReload(stateWithGain(0.5), true, false, "Agent: remove pauses", nullptr));
        REQUIRE(store->applyAgentState(stateWithGain(0.25), pauses, nullptr));

        THEN("the agent's next verb does not extend into it") {
            REQUIRE(numUndoSteps() == 2);
            REQUIRE(undoManager->undo());
            REQUIRE(container->getMasterGain() == Catch::Approx(0.5f));
            REQUIRE(undoManager->getUndoDescription() == "Agent: remove pauses");
        }
    }

    GIVEN("two states that differ only in which clip is selected") {
        json selected = { { "audium", { { "audio_tracks", json::array ({ { { "play_list_vector", json::array ({ { { "selected", true } } }) } } }) } } } };
        json unselected = selected;
        unselected["audium"]["audio_tracks"][0]["play_list_vector"][0]["selected"] = false;
        REQUIRE(selected != unselected);

        THEN("they are the same document: a click is not an edit") {
            REQUIRE(ProjectFileStore::documentOf(selected) == ProjectFileStore::documentOf(unselected));
        }
    }
}
