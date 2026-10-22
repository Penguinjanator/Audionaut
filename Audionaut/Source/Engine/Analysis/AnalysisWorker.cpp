//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "Engine/Analysis/AnalysisWorker.h"
#include "Engine/Analysis/AnalysisProvider.h"

#include <algorithm>

namespace audium {

AnalysisWorker::AnalysisWorker(std::shared_ptr<AnalysisProvider> analysisProvider_) :
    AnalysisWorker(std::move(analysisProvider_), AnalysisProvider::getMergeAnalysisTypes())
{
}

AnalysisWorker::AnalysisWorker(std::shared_ptr<AnalysisProvider> analysisProvider_,
                               std::vector<AnalysisType> defaultAnalysisTypes) :
    analysisProvider(std::move(analysisProvider_)),
    defaultTypes(std::move(defaultAnalysisTypes))
{
    // Lowest priority: analysis must never compete with the UI or audio path.
    thread.startThread(juce::Thread::Priority::background);
    thread.addTimeSliceClient(this);
}

const std::vector<AnalysisType>& AnalysisWorker::canonicalAnalysisTypes()
{
    static const std::vector<AnalysisType> types {
        AnalysisType::Segments,
        AnalysisType::Beats,
        AnalysisType::Onsets
    };
    return types;
}

std::vector<AnalysisType> AnalysisWorker::getDefaultAnalysisTypes() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return defaultTypes;
}

void AnalysisWorker::setDefaultAnalysisTypes(const std::vector<AnalysisType>& types)
{
    std::vector<AnalysisType> canonical;
    for (auto type : canonicalAnalysisTypes())
        if (std::find(types.begin(), types.end(), type) != types.end())
            canonical.push_back(type);

    std::lock_guard<std::mutex> lock(mutex);
    defaultTypes = std::move(canonical);
}

AnalysisWorker::~AnalysisWorker()
{
    // Abandon the work rather than finish it: the queue is dropped and the
    // analysis running right now (if any) is told to give up at its next
    // stage, so the wait below is bounded by a single analysis stage instead
    // of the rest of the file. The thread is never killed - a thread killed
    // inside the decoder or the allocator would leave locks held that the
    // exiting main thread then blocks on, which is worse than the wait.
    abortRequested = true;
    cancelAll();
    thread.signalThreadShouldExit();

    // Detach before stopping so removeTimeSliceClient() waits for a
    // useTimeSlice() still in progress to return, then tear the thread down.
    thread.removeTimeSliceClient(this);
    thread.stopThread(-1);
}

int AnalysisWorker::enqueue(const juce::File& audioFile,
                            const std::vector<AnalysisType>& types)
{
    if (! audioFile.existsAsFile())
        return 0;

    int queued = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto type : types)
        {
            Job job { audioFile, type };
            if (std::find(jobs.begin(), jobs.end(), job) == jobs.end())
            {
                jobs.push_back(job);
                ++queued;
            }
        }
    }

    // Wake the thread so it picks up the new work without waiting out its idle poll.
    thread.notify();
    return queued;
}

int AnalysisWorker::enqueue(const juce::File& audioFile)
{
    if (! autoAnalysisEnabled.load())
        return 0;

    // Snapshot outside enqueue()'s lock: the mutex is not recursive.
    std::vector<AnalysisType> types;
    {
        std::lock_guard<std::mutex> lock(mutex);
        types = defaultTypes;
    }
    return enqueue(audioFile, types);
}

void AnalysisWorker::cancel(const juce::File& audioFile)
{
    std::lock_guard<std::mutex> lock(mutex);
    jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
                              [&audioFile] (const Job& job) { return job.file == audioFile; }),
               jobs.end());
}

void AnalysisWorker::cancelAll()
{
    std::lock_guard<std::mutex> lock(mutex);
    jobs.clear();
}

int AnalysisWorker::useTimeSlice()
{
    juce::File file;
    std::vector<AnalysisType> types;
    {
        // Claiming the jobs and marking them running happen in one critical
        // section, so the remaining counts never miss a job that has been
        // popped but not yet flagged as running. Every queued job of the
        // first file goes together: one decode serves all of its analyses.
        std::lock_guard<std::mutex> lock(mutex);
        if (jobs.empty())
            return 500; // idle: poll again in half a second

        file = jobs.front().file;
        for (auto it = jobs.begin(); it != jobs.end();)
        {
            if (it->file == file)
            {
                types.push_back(it->type);
                it = jobs.erase(it);
            }
            else
                ++it;
        }

        currentFileName = file.getFileName();
        currentFile = file;
        currentAnalysisTypes = types;
        currentProgress = 0.0f;
        busy = true;
    }

    if (analysisProvider != nullptr && ! abortRequested.load())
        analysisProvider->analyzeFile(file, types, &abortRequested,
                                      [this](double fraction) { currentProgress = (float) fraction; });

    {
        std::lock_guard<std::mutex> lock(mutex);
        busy = false;
        currentProgress = 0.0f;
        currentFileName = {};
        currentFile = juce::File();
        currentAnalysisTypes.clear();
    }

    // Come straight back if more work is waiting, otherwise idle.
    std::lock_guard<std::mutex> lock(mutex);
    return jobs.empty() ? 500 : 0;
}

int AnalysisWorker::getPendingCount() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return static_cast<int>(jobs.size());
}

int AnalysisWorker::getRemainingCount() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return static_cast<int>(jobs.size() + currentAnalysisTypes.size());
}

int AnalysisWorker::getRemainingCount(const juce::File& audioFile) const
{
    std::lock_guard<std::mutex> lock(mutex);

    auto count = static_cast<int>(std::count_if(jobs.begin(), jobs.end(),
        [&audioFile] (const Job& job) { return job.file == audioFile; }));

    if (currentFile == audioFile && currentFile != juce::File())
        count += static_cast<int>(currentAnalysisTypes.size());

    return count;
}

juce::String AnalysisWorker::getCurrentFileName() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return currentFileName;
}

juce::File AnalysisWorker::getCurrentFile() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return currentFile;
}

std::optional<AnalysisType> AnalysisWorker::getCurrentAnalysisType() const
{
    std::lock_guard<std::mutex> lock(mutex);
    if (currentAnalysisTypes.empty())
        return std::nullopt;
    return currentAnalysisTypes.front();
}

std::vector<AnalysisType> AnalysisWorker::getCurrentAnalysisTypes() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return currentAnalysisTypes;
}

} // namespace audium
