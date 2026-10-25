//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once

#include <JuceHeader.h>
#include "Engine/Resource/AudioResourceContainer.h"
#include "Engine/PlayList/PlayListContainer.h"
#include "Interface/Widgets/audium_ListBox.h"
#include "Interface/Components/MiddlePanel/ChannelView/ChannelComponent.h"
#include "Engine/AudiumEngine.h"
#include "Engine/Group/AudioTrackContainer.h"
#include "Engine/Channel/AudioChannel.h"
#include "Interface/Models/TrackListBoxModels.h"

class ChannelSubGroupListBoxModel  : public ChannelRowsListBoxModel
{
public:
    ChannelSubGroupListBoxModel(audium::ListBox& owner,
                                std::shared_ptr<audium::AudiumEngine> audiumEngine,
                                std::shared_ptr<audium::AudioTrack> audioTrack) :
        ChannelRowsListBoxModel(owner, audiumEngine, audioTrack, 50)
    {
    }
    
    int getNumRows() override
    {
        return audioTrack->getNumAudioTrackChannels();
    }
    
    juce::Component* refreshComponentForRow (   int rowNumber, bool isRowSelected,
                                                juce::Component* existingComponentToUpdate) override
    {
        if (auto channel = audioTrack->getChannel(rowNumber)) {
            if (existingComponentToUpdate == nullptr) {
                return new ChannelComponent(audioTrack, audiumEngine, rowNumber);
            }
            else {
                auto component = dynamic_cast<ChannelComponent*>(existingComponentToUpdate);
                jassert(component);
                
                if (component != nullptr)
                {
                    // update of audioTrack since row might have changed after delete
                    component->refreshComponent(audioTrack, rowNumber, isRowSelected);
                }
                return component;
            }
        }
        
        
        return nullptr;
    }

    void selectedRowsChanged (int lastRowSelected) override
    {
        auto selectedRows = owner.getSelectedRows();
        audioTrack->audioChannelContainer->setSelectedRows(selectedRows);
        audiumEngine->getAudioTrackContainer()->sendActionMessage(audium::updateArrangementAction);
    }
    
    void setAudioTrack(std::shared_ptr<audium::AudioTrack> track)
    {
        audioTrack = track;
    }


private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelSubGroupListBoxModel)
};
