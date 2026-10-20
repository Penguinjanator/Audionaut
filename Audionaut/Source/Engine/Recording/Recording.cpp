//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "Recording.h"
#include "Engine/Resource/AudioResourceContainer.h"


namespace audium {

int Recording::recordingCounter = 0;

Recording::Recording(std::shared_ptr<ProjectDirectories> projectDirectories_) :
    projectDirectories(std::move(projectDirectories_)),
    audioThumbnailCache(64)
{
    jassert(projectDirectories != nullptr);
    formatManager.registerBasicFormats();
}

Recording::~Recording()
{
    // no callback runs any more: everything may go, acknowledged or not
    retiredRecorders.clear();
    for (auto& recorder : recorders)
        recorder = nullptr;
}

void Recording::record(bool start,
                       const int channelNumber,
                       const double positionClocks)
{
    releaseRetiredRecorders();

    if (channelNumber < 0) {
        
        for (auto i = 0; i < MAX_AUDIO_CHANNELS; ++i) {
            if (auto recorder = getAudioRecorder(i)) {
                record(start, i, positionClocks); // recursion
            }
        }
    }
    else {
        if (auto recorder = getAudioRecorder(channelNumber)) {
            if (start) {
                recordingStartPositionClocks[channelNumber] = positionClocks;
                // std::cout << "start rec " << channelNumber << " pos " << positionClocks << std::endl;
                // the package's audio directory for a saved project, the session's temp package otherwise
                const auto audioDirectory = AudioResourceContainer::getAudioFileDirectory(projectDirectories->getSerializationBaseDirectory());
                setRecordedFile(channelNumber, recorder->prepareRecording(AudioResourceContainer::getAudioRecordingFile(audioDirectory,
                                                                                                                        Recording::recordingCounter,
                                                                                                                        channelNumber),
                                                                          sampleRate));
                recorder->createRecordingThumbnail(formatManager, audioThumbnailCache);
                recorder->start();
            }
            else {
                recorder->stop();
            }
        }
        else {
            // starting needs an armed channel; stopping one that was
            // disarmed mid-take is fine, its take closed at retirement
            jassert(! start);
        }
    }
}

AudioRecorder* Recording::setRecordEnabled(const int channelNumber,
                                           bool bEnabled)
{
    //std::cout << "setRecordEnabled " << channelNumber << " " << bEnabled << std::endl;

    if (channelNumber < 0 || channelNumber >= MAX_AUDIO_CHANNELS) {
        jassertfalse;
        return nullptr;
    }

    releaseRetiredRecorders();
    ++commandsIssued; // the publish command the caller pushes for this call

    auto& recorder = recorders[channelNumber];

    if (bEnabled) {
        if (recorder == nullptr)
            recorder = std::make_shared<AudioRecorder>();

        return recorder.get();
    }

    if (recorder != nullptr) {
        // Close the take here (writer flush, thumbnail drain) - the audio
        // thread stops feeding it once the publish command has run, and
        // only then is the object itself destroyed, see releaseRetiredRecorders.
        recorder->stop();
        retiredRecorders.push_back ({ std::move (recorder), commandsIssued });
        recorder = nullptr;
    }

    return nullptr;
}

void Recording::publishRecorder(const int channelNumber, AudioRecorder* recorder) noexcept
{
    if (channelNumber >= 0 && channelNumber < MAX_AUDIO_CHANNELS)
        audioThreadRecorders[channelNumber].store (recorder, std::memory_order_release);

    commandsAcknowledged.fetch_add (1, std::memory_order_release);
}

void Recording::releaseRetiredRecorders()
{
    const auto acknowledged = commandsAcknowledged.load (std::memory_order_acquire);

    std::erase_if (retiredRecorders, [acknowledged] (const RetiredRecorder& retired) {
        return retired.ticket <= acknowledged;
    });
}

std::shared_ptr<AudioRecorder> Recording::getAudioRecorder(int channelNumber) const
{
    if (channelNumber >= 0 && channelNumber < MAX_AUDIO_CHANNELS)
        return recorders[channelNumber];

    return nullptr;
}

} // namespace audium
