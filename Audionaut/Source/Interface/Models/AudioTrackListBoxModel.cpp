//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include <iostream>

#include "AudioTrackListBoxModel.h"
#include "Engine/Group/AudioTrackContainer.h"
#include "Engine/AudiumEngine.h"
#include "Engine/ActionMessages.h"
#include "Interface/Components/MiddlePanel/AudioTrackBaseComponent.h"

juce::Component* AudioTrackListBoxModel::refreshComponentForRow (int rowNumber, bool isRowSelected,
                                                                     juce::Component* existingComponentToUpdate)
{
    auto audioTrack = audiumEngine->getAudioTrackContainer()->getAudioTrack(rowNumber);
    if (existingComponentToUpdate == nullptr) {
        if (audioTrack != nullptr)
            return new AudioTrackComponent(audioTrack, audiumEngine, zoomHandler, regionSelector);
    }
    else {
        auto component = dynamic_cast<AudioTrackBaseComponent*>(existingComponentToUpdate);
        jassert(component);
    
        if (audioTrack != nullptr) {
            // update of audioTrack since row might have changed after delete
            component->refreshComponent(audioTrack);
        }
        return component;
    }
    
    return nullptr;
}

void AudioTrackListBoxModel::backgroundClicked (const juce::MouseEvent&)
{
    owner->deselectAllRows();
    audiumEngine->getAudioTrackContainer()->getSelectionManager()->deselectAll();
    audiumEngine->getAudioTrackContainer()->sendActionMessage(audium::updateAll);
}

void AudioTrackListBoxModel::listWasScrolled()
{
    audiumEngine->getAudioTrackContainer()->sendActionMessage(audium::scrolledVertically);
}

void AudioTrackListBoxModel::selectedRowsChanged (int lastRowSelected)
{
    auto selectedRows = owner->getSelectedRows();
    audiumEngine->getAudioTrackContainer()->setSelectedRows(selectedRows);
    audiumEngine->getAudioTrackContainer()->sendActionMessage(audium::updateAll);
}
