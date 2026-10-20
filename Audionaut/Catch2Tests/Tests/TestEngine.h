#pragma once

#include <JuceHeader.h>

#include "Engine/Factory/AudiumFactory.h"
#include "Engine/AudiumEngine.h"
#include "Engine/Project/ProjectFileStore.h"
#include "Engine/Group/AudioTrackContainer.h"
#include "Engine/Group/AudioTrack.h"

/// One engine for one test case, brought up and torn down the way the
/// engine needs it.
///
///     TestEngine engine;                        // message manager + lock + engine
///     REQUIRE(engine.open(audioFileOrPackage)); // optional
///     auto track = engine.track(0);
///     engine->getPlayListScheduler()->commitPlayListData();
///
/// The constructor creates the JUCE MessageManager (the engine's async
/// updaters and change broadcasters need one), takes the message manager
/// lock for the test thread and creates an engine. The destructor releases
/// the engine first, then runs DeletedAtShutdown::deleteAll(), gives the
/// lock back and deletes the MessageManager - the order the hand-written
/// teardowns had to get right in every test.
///
/// Declare it before anything taken from the engine (tracks, items,
/// exporters): C++ destroys later locals first, so nothing declared after
/// the fixture can outlive the engine. A shared_ptr<AudioTrack> that did
/// outlive it crashed the suite before PR #98.
///
/// A test that only needs the message thread, or that creates its engines
/// itself, keeps doing so by hand; this fixture is for the common case of
/// exactly one engine per test-case pass.
class TestEngine
{
public:
    TestEngine()
    {
        juce::MessageManager::getInstance();
        lock = std::make_unique<juce::MessageManagerLock>(juce::Thread::getCurrentThread());
        engine = audium::AudiumFactory::createAudiumEngine();
    }

    ~TestEngine()
    {
        engine = nullptr;
        juce::DeletedAtShutdown::deleteAll();
        lock = nullptr;
        juce::MessageManager::deleteInstance();
    }

    /// Opens an audio file (imported onto a new track) or an .audium package.
    bool open(const juce::File& file) const
    {
        return engine->getProjectFileStore()->open(file, nullptr);
    }

    audium::AudiumEngine* operator->() const { return engine.get(); }
    audium::AudiumEngine& operator*() const { return *engine; }

    /// The engine as the shared_ptr the engine's own services take.
    const std::shared_ptr<audium::AudiumEngine>& ptr() const { return engine; }

    std::shared_ptr<audium::AudioTrackContainer> tracks() const
    {
        return engine->getAudioTrackContainer();
    }

    std::shared_ptr<audium::AudioTrack> track(int index) const
    {
        return tracks()->getAudioTrack(index);
    }

private:
    std::unique_ptr<juce::MessageManagerLock> lock;
    std::shared_ptr<audium::AudiumEngine> engine;

    JUCE_DECLARE_NON_COPYABLE(TestEngine)
};

/// A disposable copy of a checked-in session package (or legacy project
/// file) from TestFiles/Sessions.
///
/// Opening a fixture in place makes it the engine's project directory, so
/// anything the test then imports or records lands in the repository. The
/// copy lives in a work directory of its own under the temp location and is
/// deleted with this object. Declare it BEFORE the TestEngine (or engine)
/// that opens it: the copy has to outlive the engine's readers.
///
///     FixtureSessionCopy session("move-channels.audium");
///     TestEngine engine;
///     REQUIRE(session.ok());
///     REQUIRE(engine.open(session.file()));
class FixtureSessionCopy
{
public:
    explicit FixtureSessionCopy(const juce::String& sessionName)
    {
        const auto source = juce::File(CURRENT_SOURCE_DIR).getChildFile("TestFiles/Sessions").getChildFile(sessionName);
        workDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("audionaut-fixture-" + sessionName)
                            .getNonexistentSibling();
        package = workDirectory.getChildFile(sessionName);
        // copyFileTo (unlike copyDirectoryTo) does not create parents
        copied = workDirectory.createDirectory()
              && (source.isDirectory() ? source.copyDirectoryTo(package)
                                       : source.copyFileTo(package));
    }

    ~FixtureSessionCopy() { workDirectory.deleteRecursively(); }

    /// Whether the fixture existed and was copied.
    bool ok() const { return copied; }

    /// The copied package (or project file): what to open.
    const juce::File& file() const { return package; }

private:
    juce::File workDirectory;
    juce::File package;
    bool copied = false;

    JUCE_DECLARE_NON_COPYABLE(FixtureSessionCopy)
};
