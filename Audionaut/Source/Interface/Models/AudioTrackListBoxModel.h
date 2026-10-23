//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once
#include <vector>

#include <JuceHeader.h>
#include "Engine/Resource/AudioResourceContainer.h"
#include "Engine/PlayList/PlayListContainer.h"
#include "Interface/Components/MiddlePanel/ArrangementView/AudioTrackComponent.h"
#include "Interface/Handlers/ZoomHandler.h"
#include "Interface/Widgets/audium_ListBox.h"
#include "Interface/Controls/AudioTrackListBox.h"
#include "Interface/Models/TrackListBoxModels.h"

class AudioTrackContainer;

class AudioTrackListBoxModel : public TrackRowsListBoxModel {
    
public:
    
    AudioTrackListBoxModel(std::shared_ptr<AudioTrackListBox> owner,
                           std::shared_ptr<audium::AudiumEngine> audiumEngine,
                           std::shared_ptr<ZoomHandler> zoomHandler,
                           std::shared_ptr<RegionSelector> regionSelector) :
        TrackRowsListBoxModel(audiumEngine),
        owner(owner),
        zoomHandler(zoomHandler),
        regionSelector(regionSelector)
    {
    }
    
    juce::Component* refreshComponentForRow (   int rowNumber, bool isRowSelected,
                                                juce::Component* existingComponentToUpdate) override;

    void backgroundClicked (const juce::MouseEvent&) override;
    
    void listWasScrolled() override;
    
    void selectedRowsChanged (int lastRowSelected) override;
        
private:
    std::shared_ptr<AudioTrackListBox> owner;
    std::shared_ptr<ZoomHandler> zoomHandler;
    std::shared_ptr<RegionSelector> regionSelector;

};
