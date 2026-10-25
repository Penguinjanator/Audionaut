//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "PlayListContainer.h"
#include "Engine/PlayList/PlayListItem.h"
#include "Engine/Region/AudioRegion.h"
#include "Engine/Region/AudioRegionContainer.h"
#include "Engine/ActionMessages.h"
#include "Engine/Group/AudioTrack.h"
#include "Engine/Group/ResourceGroup.h"
#include "Engine/Group/AudioRegionAdapter.h"
#include "Engine/Group/AudioTrackContainer.h"
#include "Engine/Channel/AudioChannel.h"
#include "Engine/Undo/UndoableContainerAction.h"

namespace audium {

namespace {

// The play list items; older projects nest them in a "play_list" object.
json playListItemsFromJson(json& input)
{
    if (input.contains("play_list_vector"))
        return input["play_list_vector"];

    // TODO: remove legacy code soon-ish
    auto jsonPlayList = input["play_list"];
    return jsonPlayList["play_list_items"];
}

} // namespace

PlayListContainer::~PlayListContainer()
{
    playListItems.cleanup();
}

std::shared_ptr<PlayListItem> PlayListContainer::createPlayListItemAtPositionUI(std::shared_ptr<AudioRegion> audioRegion,
                                                                                double position,
                                                                                audium::TimeContextType context)
{
    // find the insert index based on position
    auto insertIndex = 0;
    // want all items before the end of this position
    auto items = itemsAtAbsoluteRange(juce::Range<double>(0.0,
                                                          position + audioRegion->getRegionData(context).getLength()), context);
    if (items.size() > 0) {
        insertIndex = getPlayListItemIndex(items.back()) + 1;
    }
    
    if (auto playListItem = createPlayListItem(audioRegion, insertIndex)) {
        playListItem->setAbsolutePosition(position, context);
        // createPlayListItem() sorted while the item still sat at its
        // provisional position; only now does it hold the position it was
        // asked for, so the order has to be restored here.
        sortByPosition();
        return playListItem;
    }

    return nullptr;
}

std::shared_ptr<PlayListItem> PlayListContainer::createPlayListItemUI(std::shared_ptr<AudioRegion> region, int insertIndex)
{
    jassert(region);
    auto playListItem = createPlayListItem(region, insertIndex);
    movePlayListItemsPosition(getPlayListItemIndex(playListItem.get()));
    return playListItem;
}

std::shared_ptr<PlayListItem> PlayListContainer::createPlayListItem(std::shared_ptr<AudioRegion> audioRegion,
                                                                    int insertIndex)
{
    jassert( insertIndex >= 0);
    jassert( insertIndex <= playListItems.size());
    
    auto itemBefore = getPlayListItem(insertIndex);
    auto thisIsTheEnd = (playListItems.size() > 0) ? playListItems.objects.back()->getAbsolutePositionRange(audium::clocks).getEnd() : 0.0;
    
    auto playListItem = std::shared_ptr<PlayListItem>(new PlayListItem(*this,
                                                                       audioRegion,
                                                                       audioRegion->getAudioTrack()->getSelectionManager()));
    playListItems.objects.insert(playListItems.objects.begin() + insertIndex, playListItem);
    
    auto pos = 0.0;
    
    if (itemBefore != nullptr)
        pos = itemBefore->getAbsolutePositionRange(audium::clocks).getStart();
    else
        pos = thisIsTheEnd;
    
    
    // in case we insert at begin
    if (insertIndex == 0) {
        auto length = playListItem->getAbsolutePositionRange(audium::clocks).getLength();
        if (pos - length >= 0.0)
            pos -= length;
    }
    
    playListItem->setAbsolutePosition(pos, audium::clocks);
    
    sortByPosition();
    
    return playListItem;
}

std::shared_ptr<PlayListItem> PlayListContainer::clonePlayListItem(std::shared_ptr<PlayListItem> item)
{
    auto context = audium::clocks;
    
    auto resourceGroup = item->getRegion()->getResourceGroup();
    auto regionContainer = resourceGroup->getAudioRegionContainer();
    auto name = regionContainer->getUniqueName(item->getRegion()->getName());
    
    auto track = item->getRegion()->getAudioTrack();
    auto newRegion = regionContainer->createRegion(name,
                                                   item->getRegionData(context),
                                                   track,
                                                   resourceGroup,
                                                   item->getRegion(),
                                                   context);

    auto newItem = createPlayListItemAtPositionUI(newRegion, item->getAbsolutePosition(context), context);
    if (newItem != nullptr) {
        newItem->getDynamics().copyFrom(item->getDynamics());
        newItem->copySpeedFrom(*item);
    }
    return newItem;
}

void PlayListContainer::movePlayListItemsPosition(int startIndex)
{
    // try to make way for item at startIndex
    auto context = audium::clocks;
    auto item = playListItems.objects.begin() + startIndex;
    if (item != playListItems.objects.end()) {
        auto range = (*item)->getAbsolutePositionRange(context);
        auto length = range.getLength();
        
        for (auto iter = playListItems.objects.begin() + startIndex + 1; iter != playListItems.objects.end(); iter++) {
            if ((*iter)->getAbsolutePositionRange(context).intersects(range)) {
                (*iter)->moveAbsolutePosition(length, context);
                std::cout << "moveAbsolutePosition: " << getPlayListItemIndex((*iter).get()) << " " << length << std::endl;
                range = (*iter)->getAbsolutePositionRange(context);
            }
            else {
                break;
            }
        }
    }
    jassert(sortedByPosition());
}

void PlayListContainer::movePlayListItemBefore(int currentIndex, int indexOfItemToPlaceBefore)
{
    auto context = audium::clocks;
    
    jassert(getPlayListItem(currentIndex));
    
    auto currentItem = getPlayListItem(currentIndex);
    auto beforeItem = getPlayListItem(indexOfItemToPlaceBefore);

    auto newPos = 0.0;
    if (beforeItem != nullptr)
        newPos = beforeItem->getAbsolutePosition(context);
    else
        newPos = playListItems.objects.back()->getAbsolutePositionRange(context).getEnd();
    
    audium::MoveItemBefore(playListItems.objects,
                           currentIndex,
                           indexOfItemToPlaceBefore);
    

    // set the new position of the dragged (current) item:
    currentItem->setAbsolutePosition(newPos, context);
    

    // move all other items to the right by duration of current item
    if (beforeItem != nullptr) {
        auto startIndex = getPlayListItemIndex(beforeItem.get());
        for (auto iter = playListItems.objects.begin() + startIndex; iter != playListItems.objects.end(); iter++) {
            (*iter)->moveAbsolutePosition(currentItem->getDurationTime(context), context);
        }
    }
    
    sortByPosition();
    
}

bool PlayListContainer::deletePlayListItem(PlayListItem* playListItem, bool deleteRegion) {
    
    if (deleteRegion) {
        for (auto resourceGroup : audioTrack.getResourceGroups()) {
            resourceGroup->getAudioRegionContainer()->deleteAudioRegion(playListItem->getRegion());
        }
    }
    
    return playListItems.deleteObject(playListItem);
}

void PlayListContainer::deletePlayListItem(int atIndex)
{
    if (atIndex >= 0 && atIndex < playListItems.size()) {
        deletePlayListItem(playListItems.getObjects()[atIndex].get(), false);
    }
}

bool PlayListContainer::deleteAssociatedItems(const AudioRegion* audioRegion)
{
    bool success = false;
    for (int i = static_cast<int>(playListItems.size() - 1); i >= 0; i--) {
        if (playListItems.getObjects()[i] != nullptr &&
            playListItems.getObjects()[i]->getRegion().get() == audioRegion)
        {
            deletePlayListItem(i);
            success = true;
        }
    }
    
    return success;
}

bool PlayListContainer::exitsInPlayList(const AudioRegion* region)
{
    auto it = std::find_if(playListItems.getObjects().begin(), playListItems.getObjects().end(), [region](const auto& item) {
        return item->getRegion().get() == region;
    });
    
    return it != playListItems.getObjects().end();
}

bool PlayListContainer::detachSharedRegion(PlayListItem& item)
{
    auto region = item.getRegion();
    const auto shared = std::any_of(playListItems.getObjects().begin(), playListItems.getObjects().end(),
                                    [&](const auto& other) {
        return other.get() != &item && other->getRegion() == region;
    });
    if (! shared)
        return false;

    auto resourceGroup = region->getResourceGroup();
    auto regionContainer = resourceGroup->getAudioRegionContainer();
    auto copy = regionContainer->createRegion(regionContainer->getUniqueName(region->getName()),
                                              item.getRegionData(audium::clocks),
                                              region->getAudioTrack(),
                                              resourceGroup,
                                              region,
                                              audium::clocks);
    item.setRegion(copy);
    return true;
}

const std::vector<std::shared_ptr<PlayListItem>> &PlayListContainer::getPlayListItems() const
{
    return playListItems.getObjects();
}

int PlayListContainer::getNumItems(std::shared_ptr<AudioTrack> track) const
{
    return static_cast<int>(playListItems.size());
}

std::shared_ptr<PlayListItem> PlayListContainer::getPlayListItem(int index) const
{
    auto i = static_cast<size_t> (index);
    if (i >= 0 && i < playListItems.size())
        return playListItems.getObjects()[i];
    return nullptr;
}

int PlayListContainer::getPlayListItemIndex(PlayListItem* item) const
{
    return playListItems.getIndex(std::dynamic_pointer_cast<const PlayListItem>(item->getSharedPtr()));
}

bool PlayListContainer::writeToJson (json& output)
{
    return writeChannelToJson(output, nullptr);
}

bool PlayListContainer::readFromJson (json& input, bool rebuild)
{
    auto jsonPlayListItems = playListItemsFromJson(input);
    
    const auto numNeeded = jsonPlayListItems.size();
    playListItems.resize (std::min (numNeeded, playListItems.size()));
    while (numNeeded > playListItems.size()) {
        playListItems.objects.emplace_back (std::make_shared<PlayListItem>(*this,
                                                                           nullptr,
                                                                           selectionManager));
    }
    
    // an item whose region can't be resolved (dangling region_id) is
    // dropped - keeping it would leave a clip with a null region behind.
    // The rest of the play list (and project) still loads, so this is
    // not reported as a failure: that would abort loading every track.
    for (auto i = 0; i < static_cast<int>(playListItems.size()); ) {
        auto &jsonElement = jsonPlayListItems[static_cast<size_t>(i)];
        if (playListItems.getObjects()[static_cast<size_t>(i)]->readFromJson(jsonElement, rebuild)) {
            ++i;
            continue;
        }
        std::cout << "error: could not load play list item, dropping it:" << std::endl;
        std::cout << jsonElement.dump(4) << std::endl;
        playListItems.objects.erase(playListItems.objects.begin() + i);
        jsonPlayListItems.erase(static_cast<size_t>(i));
    }
    
    jassert(sortedByPosition());
    return true;
}

bool PlayListContainer::writeChannelToJson(json& output, AudioChannel* audioChannel)
{
    jassert(sortedByPosition());

    for (auto item : playListItems.getObjects())
    {
        json j;
        if (item->writeToJson(j)) {
            if (audioChannel != nullptr) {
                // the full vector is indexed by source channels - meaningless at the destination
                j.erase("gain_vector");
                j["channel_clip_gain"] = item->getDynamics().getGain(audioChannel->getChannelNumber());
            }
            output["play_list_vector"] += j;
        }
    }

    return true;
}

void PlayListContainer::mergeFromJson(json& input, int destinationChannel)
{
    auto jsonPlayListItems = playListItemsFromJson(input);

    for (auto& jsonElement : jsonPlayListItems) {

        if (auto playListItem = createPlayListItemFromJson(jsonElement)) {
            jsonElement["track_id"] = playListItem->getRegion()->getAudioTrack()->getId();
            if (playListItem->readFromJson(jsonElement, false)) {

                auto clipGain = jsonElement.contains("channel_clip_gain")
                              ? jsonElement["channel_clip_gain"].template get<double>()
                              : 1.0;

                if (auto existingItem = findExistingItem(playListItem)) {
                    // item was already merged from a previous channel - only add this channel's gain
                    if (destinationChannel >= 0)
                        existingItem->getDynamics().setGain(destinationChannel, clipGain);
                }
                else {
                    if (destinationChannel >= 0)
                        playListItem->getDynamics().setGain(destinationChannel, clipGain);
                    playListItems.push_back(playListItem);
                }
            }
        }
    }
    jassert(sortedByPosition());
}


std::shared_ptr<PlayListItem> PlayListContainer::findExistingItem(std::shared_ptr<PlayListItem> other) const
{
    for (auto item : playListItems.getObjects()) {
        if (item->getAbsolutePositionRange(audium::clocks) == other->getAbsolutePositionRange(audium::clocks) &&
            item->getRegion() == other->getRegion()) {
            return item;
        }
    }
    return nullptr;
}

std::shared_ptr<PlayListItem> PlayListContainer::createPlayListItemFromJson (json& input)
{
    std::shared_ptr<PlayListItem> playListItem = nullptr;
    
    auto regionId = input["region_id"].template get<int>();
    
    auto resourceGroupId = 0;
    if (input.contains("sub_group_id")) {
        resourceGroupId = input["sub_group_id"].template get<int>(); // lecacy
    }
    else if (input.contains("resource_group_id")) {
        resourceGroupId = input["resource_group_id"].template get<int>();
    }
    
    if (audioTrack.resourceGroupContainer->objectExistsAtIndex(resourceGroupId)) {
        auto resourceGroup = audioTrack.resourceGroupContainer->getObjects()[resourceGroupId];
        jassert(resourceGroup);
        if (auto audioRegion = resourceGroup->getAudioRegionContainer()->getRegion(regionId)) {
            playListItem = std::shared_ptr<PlayListItem> (new PlayListItem(*this,
                                                                           audioRegion,
                                                                           audioTrack.getSelectionManager()));
            playListItem->readFromJson(input, true);
        }
    }
    
    return playListItem;
}

void PlayListContainer::sortByPosition()
{
    std::sort(playListItems.objects.begin(), playListItems.objects.end(),
              [](const std::shared_ptr<PlayListItem> i1, const std::shared_ptr<PlayListItem> i2)
              {
        return (i1->getAbsolutePosition(audium::clocks) < i2->getAbsolutePosition(audium::clocks));
    });
    jassert(sortedByPosition());
}

bool PlayListContainer::sortedByPosition() const
{
    auto position = 0.0;
    for (auto item : playListItems.getObjects()) {
        auto itemPos = item->getAbsolutePosition(audium::clocks);
        if (itemPos < position)
            return false;
        
        position = itemPos;
    }
    
    return true;
}

std::vector<PlayListItem*> PlayListContainer::crossfadeOverlappingNeighbours(const std::vector<PlayListItem*>& selectedItemsIn)
{
    const auto context = audium::clocks;
    std::set<std::pair<PlayListItem*, PlayListItem*>> pairs;

    for (auto* item : selectedItemsIn) {
        if (item == nullptr || getPlayListItemIndex(item) < 0)
            continue;

        const auto range = item->getAbsolutePositionRange(context);
        PlayListItem* left = nullptr;
        PlayListItem* right = nullptr;
        auto leftOverlap = 0.0, rightOverlap = 0.0;

        // the partner on each side with the largest partial overlap; a clip
        // containing the other (or contained by it) has no joint to fade
        for (auto& other : playListItems.getObjects()) {
            if (other.get() == item)
                continue;

            const auto otherRange = other->getAbsolutePositionRange(context);
            if (otherRange.getStart() < range.getStart()
                && otherRange.getEnd() > range.getStart()
                && otherRange.getEnd() < range.getEnd()
                && otherRange.getEnd() - range.getStart() > leftOverlap) {
                left = other.get();
                leftOverlap = otherRange.getEnd() - range.getStart();
            }
            if (otherRange.getStart() > range.getStart()
                && otherRange.getStart() < range.getEnd()
                && otherRange.getEnd() > range.getEnd()
                && range.getEnd() - otherRange.getStart() > rightOverlap) {
                right = other.get();
                rightOverlap = range.getEnd() - otherRange.getStart();
            }
        }

        if (left != nullptr)
            pairs.insert({ left, item });
        if (right != nullptr)
            pairs.insert({ item, right });
    }

    std::vector<PlayListItem*> fadedItems;
    for (auto& [left, right] : pairs) {
        const auto leftRange = left->getAbsolutePositionRange(context);
        const auto rightRange = right->getAbsolutePositionRange(context);
        const auto overlap = leftRange.getEnd() - rightRange.getStart();

        // fades are fractions of the clip; the timeline length scales the
        // same way as the source window, so this also holds for stretched
        // and re-pitched clips
        left->getDynamics().setFadeOutEnd(0.0);
        left->getDynamics().setFadeOut(overlap / leftRange.getLength());
        right->getDynamics().setFadeInStart(0.0);
        right->getDynamics().setFadeIn(overlap / rightRange.getLength());

        fadedItems.push_back(left);
        fadedItems.push_back(right);
    }

    return fadedItems;
}

double PlayListContainer::findNextFreePosition(double position, audium::TimeContextType context) const
{
    auto &pos = position;
    while (auto item = itemAtAbsolutePosition(pos, context)) {
        pos = item->getAbsolutePositionRange(context).getEnd();
    }
    
    return pos;
}


PlayListItem* PlayListContainer::itemAtAbsolutePosition(double position, audium::TimeContextType context) const
{
    for (auto item : playListItems.getObjects())
    {
        auto startTime = item->getAbsolutePosition(context);
        auto endTime = startTime + item->getDurationTime(context);
        juce::Range<double> absoluteRange(startTime, endTime);
        if (absoluteRange.contains(position))
        {
            return item.get();
        }
    }
    
    return nullptr;
}

PlayListItem* PlayListContainer::itemIntersectingRange(juce::Range<double> range, audium::TimeContextType context) const
{
    for (auto item : playListItems.getObjects()) {
        auto absoluteRange = item->getAbsolutePositionRange(context);
        if (absoluteRange.intersects(range))
            return item.get();
    }
    
    return nullptr;
}

PlayListItem* PlayListContainer::itemContainingRange(juce::Range<double> range, audium::TimeContextType context) const
{
    for (auto item : playListItems.getObjects()) {
        auto absoluteRange = item->getAbsolutePositionRange(context);
        if (absoluteRange.contains(range))
            return item.get();
    }
    
    return nullptr;
}


const std::vector<PlayListItem*> PlayListContainer::itemsAtAbsoluteRange(juce::Range<double> range, audium::TimeContextType context) const
{
    std::vector<PlayListItem*> result;
    
    for (auto item : playListItems.getObjects()) {
        auto absoluteRange = item->getAbsolutePositionRange(context);
        if (absoluteRange.contains(range))
            result.push_back(item.get());
    }
    
    return result;
}

void PlayListContainer::selectPlayListItemWithRegion(std::shared_ptr<AudioRegion> region)
{
    for (auto item : playListItems.getObjects())
    {
        if (item->getRegion() == region)
        {
            item->setSelected(true);
        }
    }
}

double PlayListContainer::getTotalLength(audium::TimeContextType context) const
{
    auto totalLength = 0.0;
    for (auto item : playListItems.getObjects())
    {
        // a fade-out extension keeps sounding past the region window - the
        // project must be long enough for the tail to ring out
        totalLength = juce::jmax(totalLength,
                                 item->getAbsolutePositionRange(context).getEnd()
                                 + item->getTailExtension(context));
    }
    return totalLength;
}


} // namespace audium
