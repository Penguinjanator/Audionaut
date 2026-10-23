//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once

#include <JuceHeader.h>
#include "Engine/AudiumEngine.h"
#include "Engine/Channel/AudioChannel.h"
#include "Engine/Group/AudioTrack.h"
#include "Engine/Group/AudioTrackContainer.h"
#include "Interface/Controls/DraggerControl.h"
#include "Interface/LookAndFeel/AudiumLookAndFeel.h"
#include "Interface/Widgets/audium_ListBox.h"

/**
 * One row per track, as tall as the track plus its dragger header - the
 * arrangement's and the channel view's track lists. Delete acts on the
 * engine selection.
 */
class TrackRowsListBoxModel : public audium::ListBoxModel
{
public:
    explicit TrackRowsListBoxModel (std::shared_ptr<audium::AudiumEngine> audiumEngine_) :
        audiumEngine (std::move (audiumEngine_))
    {
    }

    int getNumRows() override
    {
        return audiumEngine->getAudioTrackContainer()->getNumItems();
    }

    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}

    int getRowHeight (int rowNumber) const override
    {
        if (auto track = audiumEngine->getAudioTrackContainer()->getAudioTrack (rowNumber))
            return track->getViewState().getTotalHeight() + DraggerControl::draggerHeight;

        return 0;
    }

    void deleteKeyPressed (int) override
    {
        audiumEngine->getAudioTrackContainer()->deleteSelectedObjects();
    }

    int getExtraSpaceAtBottom() const override
    {
        return AudiumLookAndFeel::extraSpaceAtBottom;
    }

protected:
    std::shared_ptr<audium::AudiumEngine> audiumEngine;
};

/**
 * One row per channel of a track, as tall as the channel - a track's channel
 * strips and a clip's channel lanes. A click on the background deselects the
 * rows; delete acts on the engine selection.
 */
class ChannelRowsListBoxModel : public audium::ListBoxModel
{
public:
    /// heightWithoutChannel: the height of a row past the track's channels.
    ChannelRowsListBoxModel (audium::ListBox& owner_,
                             std::shared_ptr<audium::AudiumEngine> audiumEngine_,
                             std::shared_ptr<audium::AudioTrack> audioTrack_,
                             int heightWithoutChannel_) :
        owner (owner_),
        audiumEngine (std::move (audiumEngine_)),
        audioTrack (std::move (audioTrack_)),
        heightWithoutChannel (heightWithoutChannel_)
    {
    }

    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}

    int getRowHeight (int rowNumber) const override
    {
        if (auto channel = audioTrack->getChannel (rowNumber))
            return channel->getChannelHeight();

        return heightWithoutChannel;
    }

    void deleteKeyPressed (int) override
    {
        audiumEngine->getAudioTrackContainer()->deleteSelectedObjects();
    }

    void backgroundClicked (const juce::MouseEvent&) override
    {
        owner.deselectAllRows();
    }

protected:
    audium::ListBox& owner;
    std::shared_ptr<audium::AudiumEngine> audiumEngine;
    std::shared_ptr<audium::AudioTrack> audioTrack;

private:
    const int heightWithoutChannel;
};
