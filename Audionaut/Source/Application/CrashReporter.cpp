//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "CrashReporter.h"
#include "Util/Preferences.h"

#include <cstdint>
#include <cstring>
#include <utility>

#if JUCE_WINDOWS
 #ifndef PSAPI_VERSION
  #define PSAPI_VERSION 2 // K32* entry points in kernel32, no psapi.lib
 #endif
 #include <windows.h>
 #include <psapi.h>
 #include <csignal>
#else
 #include <csignal>
 #include <execinfo.h>
 #include <fcntl.h>
 #include <unistd.h>
 #if JUCE_MAC
  #include <mach-o/dyld.h>
 #endif
#endif

#include "Util/DistributionChannel.h"

namespace audium {

namespace {

// Same relay and repository as report_bug in Tools/audionaut-mcp/index.js.
const char* const relayUrl = "https://audionaut-feature-requests.feature-request-relay.workers.dev";
const char* const newIssueUrl = "https://github.com/kvoltmer/Audionaut/issues/new";

// The relay caps issue bodies at 8000 characters. A prefilled browser URL
// has to stay below the usual ~8 KB URL limits once percent-encoded, which
// triples every space and newline of a backtrace: the body is cut until
// the encoded URL fits.
constexpr int relayBodyLimit = 7800;
constexpr int urlBodyLimit = 3000;
constexpr int urlBodyStep = 500;
constexpr int urlLengthLimit = 7000;

//==============================================================================
// Crash-time state. The handler runs inside a dying process (often inside
// a signal handler), so it must not allocate or take locks: everything it
// writes is prepared here by install(). This is process-wide state by
// nature - a crash handler is per process, not per engine.

char reportHeader[2048] {};

#if JUCE_WINDOWS
wchar_t reportPathWide[1024] {};
using ReportHandle = HANDLE;

// The modules as loaded at install time. Resolving an address through the
// loader (GetModuleHandleEx) takes the loader lock, which the crashing
// thread may hold - so the handler looks addresses up here instead. A
// module loaded later shows as a bare address.
struct ModuleEntry
{
    std::uintptr_t base = 0;
    std::size_t size = 0;
    char name[64] {};
};

ModuleEntry modules[256];
int numModules = 0;
#else
char reportPath[2048] {};
using ReportHandle = int;

// Where the handler runs when the crash is a stack overflow (main thread
// only: an alternate stack is per thread, and this is the one that recurses
// through the model).
alignas (16) char alternateStack[128 * 1024];
#endif

void writeRaw (ReportHandle out, const char* text, size_t length)
{
#if JUCE_WINDOWS
    DWORD written = 0;
    WriteFile (out, text, (DWORD) length, &written, nullptr);
#else
    while (length > 0)
    {
        const auto written = ::write (out, text, length);
        if (written <= 0)
            return;
        text += written;
        length -= (size_t) written;
    }
#endif
}

void writeText (ReportHandle out, const char* text)
{
    writeRaw (out, text, std::strlen (text));
}

void writeHex (ReportHandle out, std::uint64_t value, int numDigits = 16)
{
    char digits[2 + 16];
    digits[0] = '0';
    digits[1] = 'x';
    for (int i = 0; i < numDigits; ++i)
        digits[2 + i] = "0123456789abcdef"[(value >> (4 * (numDigits - 1 - i))) & 0xf];
    writeRaw (out, digits, (size_t) (2 + numDigits));
}

#if JUCE_WINDOWS

// Writes "module.dll+0x1234" for a code address, or the bare address.
void writeModuleOffset (HANDLE out, const void* address)
{
    const auto value = (std::uintptr_t) address;

    for (int i = 0; i < numModules; ++i)
    {
        const auto& module = modules[i];
        if (value >= module.base && value < module.base + module.size)
        {
            writeText (out, module.name);
            writeText (out, "+");
            writeHex (out, (std::uint64_t) (value - module.base));
            return;
        }
    }

    writeHex (out, (std::uint64_t) value);
}

void writeBacktrace (HANDLE out)
{
    writeText (out, "\n\nBacktrace:\n");

    void* frames[128];
    const auto count = CaptureStackBackTrace (0, 128, frames, nullptr);
    for (USHORT i = 0; i < count; ++i)
    {
        writeModuleOffset (out, frames[i]);
        writeText (out, "\n");
    }
}

HANDLE openReport()
{
    return CreateFileW (reportPathWide, GENERIC_WRITE, 0, nullptr,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

// Hardware faults and other SEH exceptions.
LONG WINAPI handleException (EXCEPTION_POINTERS* info)
{
    const auto out = openReport();

    if (out != INVALID_HANDLE_VALUE)
    {
        writeText (out, reportHeader);
        writeText (out, "Crash: exception ");
        writeHex (out, info->ExceptionRecord->ExceptionCode, 8);
        writeText (out, " at ");
        writeModuleOffset (out, info->ExceptionRecord->ExceptionAddress);
        writeBacktrace (out);
        CloseHandle (out);
    }

    // let Windows Error Reporting see the crash too
    return EXCEPTION_CONTINUE_SEARCH;
}

// abort(), which an uncaught C++ exception (std::terminate) and a failed
// assertion end in. The UCRT raises SIGABRT first and then dies through
// __fastfail, which never reaches the unhandled-exception filter above -
// so without this, that whole class of crash left no report on Windows.
void handleAbort (int)
{
    const auto out = openReport();

    if (out != INVALID_HANDLE_VALUE)
    {
        writeText (out, reportHeader);
        writeText (out, "Crash: abort (uncaught exception or failed assertion)");
        writeBacktrace (out);
        CloseHandle (out);
    }

    // returning lets abort() carry on into Windows Error Reporting
}

void snapshotModules()
{
    HMODULE handles[256];
    DWORD bytesNeeded = 0;

    if (! EnumProcessModules (GetCurrentProcess(), handles, sizeof (handles), &bytesNeeded))
        return;

    const auto count = juce::jmin ((int) (bytesNeeded / sizeof (HMODULE)), (int) (sizeof (handles) / sizeof (handles[0])));

    for (int i = 0; i < count; ++i)
    {
        MODULEINFO info {};
        if (! GetModuleInformation (GetCurrentProcess(), handles[i], &info, sizeof (info)))
            continue;

        auto& entry = modules[numModules];
        entry.base = (std::uintptr_t) info.lpBaseOfDll;
        entry.size = (std::size_t) info.SizeOfImage;

        wchar_t path[MAX_PATH] {};
        GetModuleFileNameW (handles[i], path, MAX_PATH);
        const wchar_t* base = path;
        for (const wchar_t* p = path; *p != 0; ++p)
            if (*p == L'\\' || *p == L'/')
                base = p + 1;
        WideCharToMultiByte (CP_UTF8, 0, base, -1, entry.name, (int) sizeof (entry.name) - 1, nullptr, nullptr);

        ++numModules;
    }
}

#else

const int fatalSignals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGSYS };

// How long the handler may take. backtrace() can block on the dynamic
// loader's lock if the crashing thread holds it (a fault inside dlopen or
// lazy binding); the alarm's default action then ends the process instead
// of leaving it hung without any report.
constexpr unsigned handlerTimeoutSeconds = 5;

const char* signalName (int signum)
{
    switch (signum)
    {
        case SIGSEGV: return "SIGSEGV (segmentation fault)";
        case SIGBUS:  return "SIGBUS (bus error)";
        case SIGILL:  return "SIGILL (illegal instruction)";
        case SIGFPE:  return "SIGFPE (arithmetic exception)";
        case SIGABRT: return "SIGABRT (abort)";
        case SIGSYS:  return "SIGSYS (bad system call)";
        default:      return "unknown signal";
    }
}

void handleCrash (int signum, siginfo_t* info, void*)
{
    ::alarm (handlerTimeoutSeconds);

    const auto out = ::open (reportPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (out >= 0)
    {
        writeText (out, reportHeader);
        writeText (out, "Crash: ");
        writeText (out, signalName (signum));
        writeText (out, " at ");
        writeHex (out, (std::uint64_t) (std::uintptr_t) (info != nullptr ? info->si_addr : nullptr));
        writeText (out, "\n\nBacktrace:\n");

        void* frames[128];
        const auto count = ::backtrace (frames, 128);
        ::backtrace_symbols_fd (frames, count, out); // writes straight to the fd, no malloc
        ::close (out);
    }

    ::alarm (0);

    // SA_RESETHAND has restored the default action; raising again (blocked
    // while in here, delivered as soon as the handler returns) makes the
    // process die of the original signal, so the system's crash report is
    // still written - with the faulting frame on top, not this handler.
    // Returning alone would only re-fault a hardware fault; a sent signal
    // (kill) would survive, and macOS's si_code can't tell the two apart.
    ::raise (signum);
}

#endif

juce::String channelName()
{
#if JUCE_MAC
    return currentDistributionChannel() == DistributionChannel::appStore ? "Mac App Store" : "GitHub (DMG)";
#elif JUCE_LINUX
    return juce::SystemStats::getEnvironmentVariable ("FLATPAK_ID", {}).isNotEmpty() ? "Flatpak" : "GitHub";
#else
    return "GitHub";
#endif
}

juce::String architectureName()
{
#if JUCE_ARM && JUCE_64BIT
    return "arm64";
#elif JUCE_ARM
    return "arm";
#elif JUCE_64BIT
    return "x86_64";
#else
    return "x86";
#endif
}

/// The value of a "Key: value" line in the report, or an empty string.
juce::String reportValue (const juce::String& report, const juce::String& key)
{
    for (const auto& line : juce::StringArray::fromLines (report))
        if (line.startsWith (key + ": "))
            return line.fromFirstOccurrenceOf (": ", false, false).trim();

    return {};
}

/// Markdown issue body; the report is cut to fit `limit` characters.
juce::String issueBody (const juce::String& report, const juce::String& comment, int limit)
{
    const auto head = juce::String ("Audionaut quit unexpectedly; the crash report below was sent from the app "
                                    "on the next launch.\n\n**What the user was doing:** ")
                      + (comment.trim().isNotEmpty() ? comment.trim() : juce::String ("_not given_"))
                      + "\n\n```text\n";
    const juce::String tail ("\n```\n");
    const juce::String cut ("\n[... truncated]");

    auto text = report.trimEnd();
    const auto budget = limit - head.length() - tail.length();

    if (text.length() > budget)
        text = text.substring (0, budget - cut.length()) + cut;

    return head + text + tail;
}

/// The browser fallback: a new-issue page with the report in the body, cut
/// until the encoded URL is short enough for browsers and GitHub.
juce::URL prefilledIssueUrl (const juce::String& title, const juce::String& report, const juce::String& comment)
{
    for (auto limit = urlBodyLimit;; limit -= urlBodyStep)
    {
        const auto url = juce::URL (newIssueUrl)
                             .withParameter ("title", title)
                             .withParameter ("labels", "bug")
                             .withParameter ("body", issueBody (report, comment, limit));

        if (url.toString (true).length() <= urlLengthLimit || limit <= urlBodyStep)
            return url;
    }
}

} // namespace

//==============================================================================
/// Posts one report on its own thread. The reporter owns it and stops it
/// before going away, so no result lands on a dead reporter and a quit
/// does not wait on the server beyond the connection timeout.
class CrashReporter::Sender final : public juce::Thread
{
public:
    Sender (CrashReporter& owner_, juce::String title_, juce::String report_, juce::String comment_) :
        juce::Thread ("Crash report sender"),
        owner (owner_),
        title (std::move (title_)),
        report (std::move (report_)),
        comment (std::move (comment_))
    {
    }

    void run() override
    {
        const auto result = postToRelay (title, issueBody (report, comment, relayBodyLimit),
                                         [this] { return ! threadShouldExit(); });

        if (threadShouldExit())
            return;

        juce::MessageManager::callAsync ([safeOwner = juce::WeakReference<CrashReporter> (&owner),
                                          result, title = title, report = report, comment = comment] {
            if (safeOwner != nullptr)
                safeOwner->presentSendResult (result, title, report, comment);
        });
    }

private:
    CrashReporter& owner;
    const juce::String title, report, comment;
};

//==============================================================================
juce::File CrashReporter::reportFile()
{
    return Preferences::applicationDataDirectory().getChildFile ("LastCrash.txt");
}

void CrashReporter::install()
{
    const auto file = reportFile();
    file.getParentDirectory().createDirectory();

    juce::String header;
    header << "Audionaut crash report\n"
           << "Version: " << ProjectInfo::versionString << "\n"
           << "Channel: " << channelName() << "\n"
           << "OS: " << juce::SystemStats::getOperatingSystemName() << " (" << architectureName() << ")\n";
#if JUCE_MAC
    header << "Load address: 0x"
           << juce::String::toHexString ((juce::pointer_sized_int) _dyld_get_image_header (0)) << "\n";
#endif

    header.copyToUTF8 (reportHeader, sizeof (reportHeader));

#if JUCE_WINDOWS
    file.getFullPathName().copyToUTF16 ((juce::CharPointer_UTF16::CharType*) reportPathWide, sizeof (reportPathWide));
    snapshotModules();
    SetUnhandledExceptionFilter (handleException);
    std::signal (SIGABRT, handleAbort);
#else
    file.getFullPathName().copyToUTF8 (reportPath, sizeof (reportPath));

    // backtrace() may load the unwinder lazily on first use; do that now,
    // not inside the signal handler
    void* frames[4];
    ::backtrace (frames, 4);

    // a stack overflow leaves no room for the handler's frame on the stack
    // that overflowed; give this (the main) thread another one for it
    stack_t stack {};
    stack.ss_sp = alternateStack;
    stack.ss_size = sizeof (alternateStack);
    stack.ss_flags = 0;
    ::sigaltstack (&stack, nullptr);

    struct sigaction action {};
    action.sa_sigaction = handleCrash;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_ONSTACK;
    sigemptyset (&action.sa_mask);

    for (auto signum : fatalSignals)
        sigaction (signum, &action, nullptr);
#endif
}

//==============================================================================
CrashReporter::CrashReporter (UsageEventLogger logUsageEvent_) :
    logUsageEvent (std::move (logUsageEvent_))
{
    // take the last run's report now: whatever crashes between here and
    // offerPendingReport() (loading the startup project, say) writes a new
    // file, and this one would be lost
    const auto file = reportFile();

    if (! file.existsAsFile())
        return;

    const auto report = file.loadFileAsString();
    const auto crashTime = file.getLastModificationTime();
    file.deleteFile(); // consumed now, so a crash while asking can't loop

    if (report.trim().isNotEmpty())
        pendingReport = report.trimEnd() + "\n\nCrash time: " + crashTime.toISO8601 (true) + "\n";
}

CrashReporter::~CrashReporter()
{
    if (sender != nullptr)
        sender->stopThread (12000); // the request's own timeout is 10 s
}

void CrashReporter::offerPendingReport()
{
    if (pendingReport.isEmpty())
        return;

    const auto report = std::exchange (pendingReport, {});

    // "SIGSEGV (segmentation fault) at 0x..." / "exception 0xc0000005 at
    // module+0x..." / "abort (...)": the kind is what precedes the address
    // and the explanation
    const auto crash = reportValue (report, "Crash");
    const auto crashKind = crash.upToFirstOccurrenceOf (" at ", false, false)
                                .upToFirstOccurrenceOf (" (", false, false);
    juce::StringPairArray params;
    params.set ("crash", crashKind);
    params.set ("crash_version", reportValue (report, "Version"));
    logUsageEvent ("app_crash", params);

    const auto title = ("Crash in Audionaut " + reportValue (report, "Version") + ": "
                        + crash.upToFirstOccurrenceOf (" at ", false, false))
                           .substring (0, 120);

    window = std::make_unique<juce::AlertWindow> (
        TRANS ("Audionaut quit unexpectedly"),
        TRANS ("The last session ended in a crash. You can send the report below to the "
               "developer - it becomes a public GitHub issue. It contains no audio or "
               "project data; please look it over before sending."),
        juce::MessageBoxIconType::WarningIcon);

    reportView = std::make_unique<juce::TextEditor>();
    reportView->setMultiLine (true);
    reportView->setReadOnly (true);
    reportView->setScrollbarsShown (true);
    reportView->setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    reportView->setText (report, false);
    reportView->setSize (560, 220);
    window->addCustomComponent (reportView.get());

    window->addTextEditor ("comment", {}, TRANS ("What were you doing? (optional)"));
    window->addButton (TRANS ("Send Report"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (TRANS ("Don't Send"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [safeThis = juce::WeakReference<CrashReporter> (this), title, report] (int pressed) {
            if (safeThis == nullptr)
                return;

            const auto comment = safeThis->window->getTextEditorContents ("comment");
            safeThis->window.reset();
            safeThis->reportView.reset();

            if (pressed == 1)
                safeThis->send (title, report, comment);
        }));
}

void CrashReporter::send (const juce::String& title, const juce::String& report, const juce::String& comment)
{
    if (sender != nullptr)
        sender->stopThread (12000);

    sender = std::make_unique<Sender> (*this, title, report, comment);
    sender->startThread();
}

CrashReporter::SendResult CrashReporter::postToRelay (const juce::String& title, const juce::String& body,
                                                      std::function<bool()> keepGoing)
{
    SendResult result;

    auto* payload = new juce::DynamicObject();
    payload->setProperty ("kind", "bug");
    payload->setProperty ("title", title);
    payload->setProperty ("body", body);
    payload->setProperty ("client", "Audionaut " + juce::String (ProjectInfo::versionString)
                                        + " crash reporter (" + juce::SystemStats::getOperatingSystemName() + ")");

    int statusCode = 0;
    const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                             .withConnectionTimeoutMs (10000)
                             .withExtraHeaders ("Content-Type: application/json\r\nUser-Agent: Audionaut/"
                                                + juce::String (ProjectInfo::versionString))
                             .withProgressCallback ([keepGoing] (int, int) { return keepGoing(); })
                             .withStatusCode (&statusCode);

    const auto stream = juce::URL (relayUrl)
                            .withPOSTData (juce::JSON::toString (juce::var (payload), true))
                            .createInputStream (options);

    if (stream == nullptr)
    {
        result.error = TRANS ("The report server could not be reached.");
        return result;
    }

    const auto reply = juce::JSON::parse (stream->readEntireStreamAsString());

    if (statusCode != 200 || ! static_cast<bool> (reply.getProperty ("success", false)))
    {
        result.error = reply.getProperty ("message", TRANS ("The report server answered with status ")
                                                         + juce::String (statusCode) + ".").toString();
        return result;
    }

    result.sent = true;
    result.issueUrl = reply.getProperty ("url", {}).toString();
    return result;
}

void CrashReporter::presentSendResult (const SendResult& result, const juce::String& title,
                                       const juce::String& report, const juce::String& comment)
{
    if (result.sent)
    {
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
            juce::MessageBoxIconType::InfoIcon,
            TRANS ("Crash report sent"),
            TRANS ("Thank you - the report was filed as a GitHub issue."),
            TRANS ("View Issue"),
            TRANS ("Close"));

        const auto issueUrl = result.issueUrl;
        juce::NativeMessageBox::showAsync (options, [issueUrl] (int pressed) {
            if (pressed == 0 && issueUrl.isNotEmpty())
                juce::URL (issueUrl).launchInDefaultBrowser();
        });
        return;
    }

    // the browser fallback needs a GitHub account but no relay
    const auto prefilled = prefilledIssueUrl (title, report, comment);

    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
        juce::MessageBoxIconType::WarningIcon,
        TRANS ("Crash report not sent"),
        result.error + "\n\n" + TRANS ("You can file it yourself as a prefilled GitHub issue instead."),
        TRANS ("Open in Browser"),
        TRANS ("Cancel"));

    juce::NativeMessageBox::showAsync (options, [prefilled] (int pressed) {
        if (pressed == 0)
            prefilled.launchInDefaultBrowser();
    });
}

} // namespace audium
