//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once

#include <memory>
#include <JuceHeader.h>

#include "Engine/Project/ProjectSerializer.h"
#include "Engine/Project/ProjectFileStore.h"

namespace audium
{

/**
 * @struct AgentEditBatch
 * @brief The agent edits that share one undo step.
 *
 * An agent reaches the open document one verb at a time, each a full project
 * state. Rather than one undo step per verb, consecutive verbs of the same
 * task extend a batch: `before` is the document before the batch's first
 * edit, `after` the document after its latest one. The undo action below
 * reads both at undo/redo time, so extending a batch never touches the undo
 * history. See `ProjectFileStore::applyAgentState` for what starts a new
 * batch.
 */
struct AgentEditBatch
{
    juce::String key;             ///< what the edits have in common (agent session + task)
    juce::String transactionName; ///< the undo step's name, fixed when the batch starts
    json before;                  ///< the document before the first edit: what undo restores
    json after;                   ///< the document after the latest edit: what redo restores
    int numEdits = 0;
    juce::Time lastEdit;
    juce::Time transactionTime; ///< the undo manager's stamp on the step, read after the first perform
};

/**
 * @struct UndoableAgentBatchAction
 * @brief The undo step behind an `AgentEditBatch`.
 *
 * `perform()` (the batch's first edit, and redo) applies the batch's current
 * `after` state, `undo()` its `before` state. The store's external-change
 * marker follows the applied state, as with `UndoableReloadAction`.
 */
struct UndoableAgentBatchAction final : public juce::UndoableAction
{
    UndoableAgentBatchAction (ProjectSerializer& serializer_, ProjectFileStore& store_,
                              std::shared_ptr<AgentEditBatch> batch_) :
        serializer (serializer_),
        store (store_),
        batch (std::move (batch_)),
        markerBefore (store_.wasChangedExternally()),
        // KB of the before-state: the UndoManager reads this once, at insertion
        sizeInUnits ((int) juce::jmax ((size_t) 1, batch->before.dump().size() / 1024))
    {
    }

    bool perform() override
    {
        return apply (batch->after, batch->before, true);
    }

    bool undo() override
    {
        return apply (batch->before, batch->after, markerBefore);
    }

    int getSizeInUnits() override { return sizeInUnits; }

private:
    /** Applies `state`; on failure restores `fallback` so a rejected state
        does not leave a half-mutated document. */
    bool apply (json& state, json& fallback, bool markerAfterwards)
    {
        try {
            if (serializer.applyProjectJson (state, true)) {
                store.setChangedExternally (markerAfterwards);
                return true;
            }
        }
        catch (std::exception& e) {
            std::cout << "UndoableAgentBatchAction -> " << e.what() << std::endl;
        }

        try {
            serializer.applyProjectJson (fallback, true);
        }
        catch (std::exception& e) {
            std::cout << "UndoableAgentBatchAction: rollback failed -> " << e.what() << std::endl;
        }
        return false;
    }

    ProjectSerializer& serializer;
    ProjectFileStore& store;
    std::shared_ptr<AgentEditBatch> batch;
    bool markerBefore = false;
    int sizeInUnits = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndoableAgentBatchAction)
};

} // namespace audium
