//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "Cli/Commands/Commands.h"
#include "Engine/Project/ProjectFileStore.h"
#include "Cli/CommandSession.h"

#include "Engine/AutoEdit/AutoEdit.h"

namespace audium {
namespace cli {

int runAutoEdit (const juce::ArgumentList& args, CliContext& context)
{
    auto working = args;
    AutoEditConfig config;
    config.trackId = takeOptionValue (working, "--track", "0").getIntValue();
    config.playlistItemId = takeOptionValue (working, "--clip", "-1").getIntValue();
    std::string optionError;
    double parsed = 0.0;
    // 0 measures is the documented "off" (segment by count instead)
    if (auto value = takeOptionValue (working, "--measures"); value.isNotEmpty()) {
        if (! parseNumericOption ("--measures", value, 0.0, false, parsed, optionError))
            return context.fail (exitUsage, "usage", optionError);
        config.segmentMeasures = parsed;
    }
    if (auto value = takeOptionValue (working, "--segments"); value.isNotEmpty()) {
        if (! parseNumericOption ("--segments", value, 1.0, false, parsed, optionError))
            return context.fail (exitUsage, "usage", optionError);
        config.numSegments = static_cast<int> (parsed);
    }
    if (auto value = takeOptionValue (working, "--duration"); value.isNotEmpty()) {
        if (! parseNumericOption ("--duration", value, 0.0, true, parsed, optionError))
            return context.fail (exitUsage, "usage", optionError);
        config.duration = parsed;
    }
    config.crossfades = ! working.removeOptionIfFound ("--no-crossfades");

    return runProjectEdit (working, context, "auto-edit", [&] (ProjectSession& session, EditOutcome& outcome)
    {
        // Auto-edit consumes cached analysis results; run `analyze` first. Its
        // own error callback reports a missing/incomplete cache.
        std::string error;
        AutoEdit autoEdit (session.get());
        if (! autoEdit.invokeAutoEdit (config, [&error] (std::string message) { error = message; }))
            return context.fail (exitFailure, "auto_edit_failed",
                                 error.empty() ? "auto-edit failed" : error);

        outcome.log = "auto-edit applied";
        outcome.result = { { "trackId", config.trackId },
                           { "numSegments", config.numSegments },
                           { "segmentMeasures", config.segmentMeasures },
                           { "crossfades", config.crossfades } };
        return exitOk;
    });
}

int runAssemble (const juce::ArgumentList& args, CliContext& context)
{
    auto working = args;
    AssembleConfig config;
    config.crossfades = ! working.removeOptionIfFound ("--no-crossfades");
    config.trackId = takeOptionValue (working, "--track", "0").getIntValue();
    if (auto value = takeOptionValue (working, "--duration"); value.isNotEmpty()) {
        std::string optionError;
        if (! parseNumericOption ("--duration", value, 0.0, true, config.duration, optionError))
            return context.fail (exitUsage, "usage", optionError);
    }
    if (auto value = takeOptionValue (working, "--seed"); value.isNotEmpty())
        config.seed = static_cast<unsigned int> (value.getLargeIntValue());

    auto mode = takeOptionValue (working, "--mode", "sequential");
    if (mode == "random")
        config.mode = AssembleConfig::Mode::Random;
    else if (mode == "sequential")
        config.mode = AssembleConfig::Mode::Sequential;
    else
        return context.fail (exitUsage, "usage", "--mode must be random or sequential");

    return runProjectEdit (working, context, "assemble", [&] (ProjectSession& session, EditOutcome& outcome)
    {
        std::string error;
        AutoEdit autoEdit (session.get());
        if (! autoEdit.invokeAssemble (config, [&error] (std::string message) { error = message; }))
            return context.fail (exitFailure, "assemble_failed",
                                 error.empty() ? "assemble failed" : error);

        outcome.log = "assemble applied";
        outcome.result = { { "trackId", config.trackId },
                           { "durationSeconds", config.duration },
                           { "mode", mode.toStdString() },
                           { "crossfades", config.crossfades } };
        return exitOk;
    });
}

} // namespace cli
} // namespace audium
