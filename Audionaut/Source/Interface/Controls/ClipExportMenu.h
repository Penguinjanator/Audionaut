//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once

#include <JuceHeader.h>
#include "Engine/AudiumEngine.h"
#include "Engine/Export/PlayListItemExport.h"
#include "Engine/PlayList/PlayListItem.h"
#include "Interface/LookAndFeel/AudiumLookAndFeel.h"

/**
 * The "Export..." context menu of a clip - the clip header in the arrangement
 * and the clip's row in the playlist table show the same one. Owns the
 * export while the file chooser and the export run, so it lives as long as
 * the component showing it.
 */
class ClipExportMenu
{
public:
    /**
     * Shows the menu for a right click on owner. The clip is looked up only
     * once "Export..." is chosen (nullptr: nothing to export).
     */
    void show (juce::Component& owner,
               std::shared_ptr<audium::AudiumEngine> audiumEngine,
               std::function<std::shared_ptr<audium::PlayListItem>()> clipToExport)
    {
        juce::PopupMenu m;
        m.addItem (TRANS ("Export..."), [this,
                                         safeOwner = juce::Component::SafePointer<juce::Component> (&owner),
                                         audiumEngine,
                                         clipToExport]
        {
            if (safeOwner == nullptr)
                return;
            if (auto item = clipToExport()) {
                exporter = std::make_unique<audium::PlayListItemExport> (audiumEngine, item, true);
                exporter->exportItem();
            }
        });
        m.setLookAndFeel (&owner.getLookAndFeel());
        m.showMenuAsync (juce::PopupMenu::Options().withStandardItemHeight (AudiumLookAndFeel::popupMenuItemHeight));
    }

private:
    std::unique_ptr<audium::PlayListItemExport> exporter;
};
