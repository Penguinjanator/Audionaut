//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once

#include <JuceHeader.h>
#include "Engine/ActionMessages.h"
#include "Engine/Group/AudioTrackContainer.h"
#include "Engine/PlayList/PlayListContainer.h"
#include "Engine/PlayList/PlayListItem.h"
#include "Engine/Region/AudioRegion.h"
#include "Engine/Region/AudioRegionContainer.h"
#include "Engine/Group/ResourceGroup.h"

namespace audium
{

/**
 * @struct UndoablePlayListItemAction
 * @brief Undo step for a clip edit: position, trim (region window), speed,
 *        stretch mode, clip gain and fades of the affected clips.
 *
 * Snapshots only the timeline state of the affected `PlayListItem`s instead
 * of the whole track container. The container snapshot's replay re-read
 * every clip from JSON, and `PlayListItem::readFromJson` calls `init()`,
 * which tears down and recreates each clip's voice sources (including a
 * Rubber Band stretcher per clip) - a DSP spike on every clip drop, with
 * playing clips restarting. Applying this step only writes the stored
 * values back through the item's setters and re-sorts its playlist, so the
 * playback chain is untouched.
 *
 * A trim that starts on a clip sharing its region with other clips first
 * gives the clip a private copy (PlayListContainer::detachSharedRegion),
 * so a step's region can differ between its two states. The new state's
 * region is then the copy this step created: undo points the clip back and
 * takes the copy out of the region list, redo puts it back.
 *
 * Regions are held weakly too, except such a copy: while it is out of the
 * region list only this step keeps it alive. A strong reference to any
 * other region would close a cycle for the steps a PlayListItem holds
 * itself (item -> step -> region -> track -> playlist -> item) and leak them.
 *
 * Items are held weakly: a step whose items were replaced by a rebuilding
 * replay (track added or removed) fails to apply, which makes the undo
 * manager drop its history instead of touching the wrong clips.
 */
struct UndoablePlayListItemAction final : public juce::UndoableAction
{
    /** The per-clip state a drag can change. */
    struct State
    {
        double positionClocks = 0.0;
        std::weak_ptr<AudioRegion> region;
        juce::Range<double> regionClocks;
        double speedRatio = 1.0;
        bool tempoLocked = false;
        double clipTempo = 0.0;
        StretchMode stretchMode = StretchMode::RePitch;
        // gain per channel, fades, curves - the item's own JSON form, so the
        // snapshot needs no access to ClipDynamics' internals
        json dynamics;

        bool operator== (const State& other) const
        {
            return positionClocks == other.positionClocks
                && sameRegion (region, other.region)
                && regionClocks == other.regionClocks
                && speedRatio == other.speedRatio
                && tempoLocked == other.tempoLocked
                && clipTempo == other.clipTempo
                && stretchMode == other.stretchMode
                && dynamics == other.dynamics;
        }
    };

    static bool sameRegion (const std::weak_ptr<AudioRegion>& a, const std::weak_ptr<AudioRegion>& b)
    {
        return ! a.owner_before (b) && ! b.owner_before (a);
    }

    UndoablePlayListItemAction (AudioTrackContainer& container_,
                                const std::vector<std::shared_ptr<PlayListItem>>& items)
        : container (container_)
    {
        for (auto& item : items)
            if (item != nullptr)
                entries.push_back ({ item, capture (*item), capture (*item) });
    }

    /** Captures the items' current state as the state perform() applies. */
    void storeNewState()
    {
        for (auto& entry : entries)
        {
            if (auto item = entry.item.lock())
            {
                entry.newState = capture (*item);
                entry.createdRegion = sameRegion (entry.oldState.region, entry.newState.region)
                                          ? nullptr : entry.newState.region.lock();
            }
        }
    }

    /** True when nothing changed between the two stored states. */
    bool isNoOp() const noexcept
    {
        return std::all_of (entries.begin(), entries.end(),
                            [] (const Entry& e) { return e.oldState == e.newState; });
    }

    bool perform() override
    {
        return apply (true);
    }

    bool undo() override
    {
        return apply (false);
    }

    int getSizeInUnits() override
    {
        return juce::jmax (1, (int) entries.size());
    }

    static State capture (const PlayListItem& item)
    {
        State state { item.getAbsolutePosition (audium::clocks),
                      item.getRegion(),
                      item.getRegionData (audium::clocks),
                      item.getSpeedRatio(),
                      item.isTempoLocked(),
                      item.getClipTempo(),
                      item.getStretchMode(),
                      json::object() };
        item.getDynamics().writeToJson (state.dynamics);
        return state;
    }

private:
    struct Entry
    {
        std::weak_ptr<PlayListItem> item;
        State oldState;
        State newState;
        // the region copy this step created (see the class comment), if any
        std::shared_ptr<AudioRegion> createdRegion;
    };

    static void restore (PlayListItem& item, const State& state, std::shared_ptr<AudioRegion> region)
    {
        // order matters (see PlayListItem::copySpeedFrom): the plain ratio
        // only takes while the clip is unlocked
        item.setTempoLocked (false);
        item.setSpeedRatio (state.speedRatio);
        item.setClipTempo (state.clipTempo);
        item.setTempoLocked (state.tempoLocked);
        item.setStretchMode (state.stretchMode);

        auto dynamics = state.dynamics;   // readFromJson wants it mutable
        item.getDynamics().readFromJson (dynamics);

        item.setRegion (region);
        item.setRegionData (state.regionClocks, audium::clocks);
        item.setAbsolutePosition (state.positionClocks, audium::clocks);
    }

    bool apply (bool toNewState)
    {
        std::vector<std::shared_ptr<PlayListItem>> items;
        std::vector<std::shared_ptr<AudioRegion>> regions;
        items.reserve (entries.size());
        regions.reserve (entries.size());

        // resolve everything first: an expired item or region means the
        // clips were rebuilt (or the region deleted) since, and a
        // half-applied step would be worse than none
        for (auto& entry : entries)
        {
            auto item = entry.item.lock();
            auto region = (toNewState && entry.createdRegion != nullptr)
                              ? entry.createdRegion
                              : (toNewState ? entry.newState.region : entry.oldState.region).lock();
            if (item == nullptr || region == nullptr)
                return false;
            items.push_back (item);
            regions.push_back (region);
        }

        // a region copy made by this step joins the region list with the
        // new state and leaves it again with the old one
        for (auto& entry : entries)
            if (entry.createdRegion != nullptr && toNewState)
                entry.createdRegion->getResourceGroup()->getAudioRegionContainer()
                    ->addAudioRegion (entry.createdRegion);

        std::vector<PlayListContainer*> playLists;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            restore (*items[i], toNewState ? entries[i].newState : entries[i].oldState, regions[i]);

            auto* playList = &items[i]->getPlayListContainer();
            if (std::find (playLists.begin(), playLists.end(), playList) == playLists.end())
                playLists.push_back (playList);
        }

        // a move can change the order the scheduler walks
        for (auto* playList : playLists)
            playList->sortByPosition();

        // only now is the copy unused by every clip
        auto regionsChanged = false;
        for (auto& entry : entries)
        {
            if (entry.createdRegion == nullptr)
                continue;

            if (! toNewState)
                entry.createdRegion->getResourceGroup()->getAudioRegionContainer()
                    ->deleteAudioRegion (entry.createdRegion);
            regionsChanged = true;
        }

        if (regionsChanged)
            container.sendActionMessage (updateRightPanelAction);

        container.sendActionMessage (updateArrangementAction);

        // the audio thread plays a committed clip snapshot, not the items:
        // the change message makes the scheduler republish it (see
        // PlayListScheduler::changeListenerCallback), and a playing voice
        // whose clip moved is stopped and rescheduled on the next block
        container.sendChangeMessage();
        return true;
    }

    AudioTrackContainer& container;
    std::vector<Entry> entries;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndoablePlayListItemAction)
};

} // namespace audium
