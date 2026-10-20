//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once

#include <JuceHeader.h>

namespace audium
{

/**
 * @class ProjectDirectories
 * @brief Where one engine's project lives on disk.
 *
 * `projectDirectory` is the package of a saved project (invalid while the
 * project was never saved); `temporaryDirectory` is the session's temp
 * package, where the audio of a never-saved project and crash-recovery
 * snapshots go. One instance per engine, created first in `AudiumFactory`
 * and shared by the collaborators that resolve paths. Several engines live
 * in one process (the app, its hosted scratch engines, tests), so this is
 * never static.
 *
 * A scratch engine hosted beside a live one borrows the live engine's
 * values (`borrowFrom`): its imports land in the live session's package or
 * temp dir, resource paths resolve against them, and its teardown never
 * deletes the temp dir.
 */
class ProjectDirectories
{
public:
    juce::File getProjectDirectory() const { return projectDirectory; }
    juce::File getTemporaryDirectory() const { return temporaryDirectory; }

    void setProjectDirectory (juce::File directory) { projectDirectory = std::move (directory); }
    void setTemporaryDirectory (juce::File directory) { temporaryDirectory = std::move (directory); }

    /**
     * @brief The directory resource paths are serialized relative to and
     *        autosaves go to: the project package for saved projects, the
     *        session's temp directory (where the audio actually lives) for
     *        never-saved ones.
     */
    juce::File getSerializationBaseDirectory() const
    {
        if (projectDirectory != juce::File() && projectDirectory.isDirectory())
            return projectDirectory;
        return temporaryDirectory;
    }

    /** Whether the temp directory is this engine's to reset and delete. */
    bool ownsTemporaryDirectory() const { return ownsTemporary; }

    /**
     * @brief Shares a live session's directories with a scratch engine.
     *
     * The scratch engine then reads and writes the live session's package and
     * temp dir but must not delete the temp dir on teardown - that would take
     * the running session's audio with it. Nor does it ever get a temp dir of
     * its own: `AudioResourceContainer::createTemporaryProjectDirectory` only
     * names one while none is set, and recreates a missing one at its path.
     */
    void borrowFrom (const ProjectDirectories& live)
    {
        projectDirectory = live.projectDirectory;
        temporaryDirectory = live.temporaryDirectory;
        ownsTemporary = false;
    }

private:
    juce::File projectDirectory;
    juce::File temporaryDirectory;
    bool ownsTemporary = true;
};

} // namespace audium
