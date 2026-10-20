//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once

#include <functional>
#include <JuceHeader.h>

namespace audium {

/**
 * @class CrashReporter
 * @brief Writes a crash log on the way down and offers to send it on the
 *        next launch.
 *
 * install() arms a fatal-signal handler (macOS / Linux) or an unhandled
 * exception filter plus a SIGABRT handler (Windows, where abort() - the end
 * of an uncaught exception - never reaches the filter). When the app dies,
 * the handler writes a plain text report - version, channel, OS, load
 * address, fault address, raw backtrace - to LastCrash.txt in the app data
 * folder and then lets the crash run its normal course, so the system's own
 * crash reporting (macOS .ips reports and the App Store's Xcode Organizer,
 * Windows Error Reporting, core dumps) keeps working. Nothing leaves the
 * machine at that point. juce::SystemStats::setApplicationCrashHandler is
 * not used because it ends in SIGKILL / EXCEPTION_EXECUTE_HANDLER, which
 * suppresses exactly that.
 *
 * The handler runs without allocating or taking locks: Windows resolves
 * addresses against a module table taken at install time rather than
 * through the loader, and the POSIX handler is bounded by an alarm in case
 * the unwinder blocks on a lock the crashing thread holds. On POSIX the
 * main thread gets an alternate signal stack, so a stack overflow there
 * still produces a report (other threads' overflows do not).
 *
 * offerPendingReport() runs on the next GUI start: if a report is waiting,
 * it shows the report, lets the user add what they were doing, and on
 * "Send Report" files it as a GitHub issue through the feature-request
 * relay that the MCP server's report_bug also uses. If the relay can't be
 * reached, it opens a prefilled issue page in the browser. The file is
 * consumed before the dialog appears, so a crash while asking can't nag
 * forever. The send runs on a thread the reporter owns and stops before it
 * goes away.
 *
 * Release builds are stripped, so the backtrace is addresses. They are
 * symbolicated by hand with the release's dSYM / PDB: on macOS use
 * `atos -o <dSYM> -l <load address> <addresses>`, on Linux use
 * `addr2line -e <binary> <offsets>`, and on Windows look up the
 * module+RVA pairs in the PDB.
 */
class CrashReporter
{
public:
    using UsageEventLogger = std::function<void (const juce::String&, const juce::StringPairArray&)>;

    /// Arms the process-wide crash handler. Call once, early in GUI startup,
    /// from the main thread; CLI verbs don't install it.
    static void install();

    /// Where the handler writes the report, in the app data folder.
    static juce::File reportFile();

    /// logUsageEvent receives one "app_crash" event per found report (the
    /// app's logger drops it unless the user opted in to usage statistics).
    explicit CrashReporter (UsageEventLogger logUsageEvent);
    ~CrashReporter();

    /// If the last run left a report, asks the user whether to send it.
    void offerPendingReport();

private:
    struct SendResult
    {
        bool sent = false;
        juce::String issueUrl;
        juce::String error;
    };

    class Sender;

    /// Blocking; `keepGoing` is polled during the transfer and false aborts it.
    static SendResult postToRelay (const juce::String& title, const juce::String& body,
                                   std::function<bool()> keepGoing);

    void send (const juce::String& title, const juce::String& report, const juce::String& comment);
    void presentSendResult (const SendResult& result, const juce::String& title,
                            const juce::String& report, const juce::String& comment);

    UsageEventLogger logUsageEvent;
    std::unique_ptr<juce::AlertWindow> window;
    std::unique_ptr<juce::TextEditor> reportView;
    std::unique_ptr<Sender> sender;

    JUCE_DECLARE_WEAK_REFERENCEABLE (CrashReporter)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrashReporter)
};

} // namespace audium
