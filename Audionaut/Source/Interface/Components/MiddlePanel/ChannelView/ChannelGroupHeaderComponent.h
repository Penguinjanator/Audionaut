//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once

#include <JuceHeader.h>

#include "Engine/Group/AudioTrack.h"
#include "Engine/Group/AudioTrackContainer.h"
#include "Engine/ActionMessages.h"
#include "Engine/Analysis/AnalysisProvider.h"
#include "Engine/Undo/UndoableEdit.h"

#include "Interface/LookAndFeel/AudiumLookAndFeel.h"
#include "Interface/Controls/AudiumLabel.h"


class ChannelGroupHeaderComponent : public juce::Component,
                                    public juce::Label::Listener,
                                    public juce::KeyListener,
                                    public juce::DragAndDropTarget
{
public:
    ChannelGroupHeaderComponent(std::shared_ptr<audium::AudioTrack> audioTrack) :
        audioTrack(audioTrack)
    {
        audioTrackNameLabel.reset (new AudiumLabel ("track name",
                                             TRANS ("n/a")));
        addAndMakeVisible (audioTrackNameLabel.get());
        
        audioTrackNameLabel->setFont (juce::FontOptions (12.00f));
        audioTrackNameLabel->setJustificationType (juce::Justification::centredLeft);
        audioTrackNameLabel->setEditable (false, true, false);
    
        // Label colours (also see updateFromEngine):
        audioTrackNameLabel->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        audioTrackNameLabel->setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        audioTrackNameLabel->setColour (juce::TextEditor::highlightColourId, juce::Colours::darkgrey);
        audioTrackNameLabel->setMinimumHorizontalScale(1.f); // avoid scaling of text
        audioTrackNameLabel->addListener (this);
        
        
        minimizeButton = std::make_unique<juce::DrawableButton>("Minimize", juce::DrawableButton::ButtonStyle::ImageOnButtonBackground);
        addAndMakeVisible(minimizeButton.get());
        minimizeButton->setClickingTogglesState(true);
        
        auto arrowSize = 15.f;
        juce::Rectangle<float> arrowZone (1.f, 1.f, arrowSize - 2.f, arrowSize - 2.f);
        Point<float> p;
        
        Path path1;
        p = Point(arrowZone.getX() + 3.f, arrowZone.getCentreY() - 2.f);
        path1.startNewSubPath (p);
        p = Point(arrowZone.getCentreX(), arrowZone.getCentreY() + 3.f);
        path1.lineTo (p);
        p = Point(arrowZone.getRight() - 3.f, arrowZone.getCentreY() - 2.f);
        path1.lineTo (p);
        juce::DrawablePath drawablePath1;
        drawablePath1.setPath(path1);
        drawablePath1.setFill (FillType(findColour (ComboBox::arrowColourId).withAlpha (0.9f)));
        
        Path path2;
        p = Point(arrowZone.getCentreX() - 2.f, arrowZone.getCentreY() + 3.f);
        path2.startNewSubPath (p);
        p = Point(arrowZone.getRight() - 3.f, arrowZone.getCentreY());
        path2.lineTo (p);
        p = Point(arrowZone.getCentreX() - 2.f, arrowZone.getCentreY() - 3.f);
        path2.lineTo (p);
        juce::DrawablePath drawablePath2;
        drawablePath2.setPath(path2);
        drawablePath2.setFill (FillType(findColour (ComboBox::arrowColourId).withAlpha (0.9f)));
        
        minimizeButton->setImages(&drawablePath1, nullptr, nullptr, nullptr, &drawablePath2);
        
        auto bc = findColour (ComboBox::backgroundColourId);
        minimizeButton->setColour (TextButton::buttonOnColourId, bc);
        minimizeButton->setColour (TextButton::buttonColourId, bc);
        
        // 19 is the dragger height. center horizontally -> 19 - 15 = 4 / 2 = 2
        minimizeButton->setBounds (5, 2, 15, 15);
        
        minimizeButton->onClick = [this]() {
            setMinimized (minimizeButton->getToggleState());
        };
   
        updateFromEngine (audioTrack);
        
        addKeyListener (this);
    }

    ~ChannelGroupHeaderComponent() override
    {
        audioTrack = nullptr;
        removeKeyListener(this);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(1, 1);
        bounds.removeFromLeft(25);
        audioTrackNameLabel->setBounds(bounds);
    }
    
    void labelTextChanged (juce::Label* labelThatHasChanged) override
    {
        if (labelThatHasChanged == audioTrackNameLabel.get())
        {
            audium::applyAsUndoableEdit(audioTrack->getAudioTrackContainer(), [this]
            {
                audioTrack->setAudioTrackName(audioTrackNameLabel->getText());
                return true;
            },
            "Set audio track name", false);
        }
    }
    
    void updateFromEngine(std::shared_ptr<audium::AudioTrack> newAudioTrack)
    {
        audioTrack = newAudioTrack;
        audioTrackNameLabel->setText (audioTrack->getAudioTrackName(), juce::dontSendNotification);
        audioTrackNameLabel->setColour (juce::Label::textColourId, audioTrack->getViewState().getColour());
        audioTrackNameLabel->setColour (juce::TextEditor::textColourId, audioTrack->getViewState().getColour());
        audioTrackNameLabel->setColour (juce::Label::textWhenEditingColourId, audioTrack->getViewState().getColour());
        
        minimizeButton->setToggleState (audioTrack->getViewState().getMinimized(), dontSendNotification);
    }
    
    void paint (juce::Graphics& g) override;
    
    void setMinimized(bool bMinimized)
    {
        audium::applyAsUndoableEdit(audioTrack->getAudioTrackContainer(), [this, bMinimized]
        {
            audioTrack->getViewState().setMinimized(bMinimized);
            return true;
        },
        "minimize audio track", false);
    }
    
    void toggleAnalysisVisibility(audium::AnalysisType analysisType)
    {
        audium::applyAsUndoableEdit(audioTrack->getAudioTrackContainer(), [this, analysisType]
        {
            audioTrack->getViewState().setAnalysisTypeVisible(analysisType, !audioTrack->getViewState().isAnalysisTypeVisible(analysisType));
            return true;
        },
        "toggle analysis visibility", false);

        // refresh the arrangement so the clips redraw with the new visibility
        audioTrack->getAudioTrackContainer().sendActionMessage(audium::updateArrangementAction);
    }

    void setChannelHeight(int height)
    {
        audium::applyAsUndoableEdit(audioTrack->getAudioTrackContainer(), [this, height]
        {
            jassert(height > 0);
            audioTrack->getViewState().setMinimized(false);
            audioTrack->getViewState().setChannelHeight(height);
            return true;
        },
        "Set audio track height", false);
    }
    
    
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) {
            auto safeThis = Component::SafePointer<ChannelGroupHeaderComponent> (this);

            PopupMenu m;
            m.setLookAndFeel (&getLookAndFeel());
            PopupMenu sizeMenu;
            auto addSize = [&sizeMenu, safeThis] (const String& name, int height) {
                sizeMenu.addItem (name, [safeThis, height] {
                    if (safeThis != nullptr)
                        safeThis->setChannelHeight (height);
                });
            };
            addSize (TRANS ("micro"), AudiumLookAndFeel::SizeIds::micro);
            addSize (TRANS ("small"), AudiumLookAndFeel::SizeIds::small);
            addSize (TRANS ("medium"), AudiumLookAndFeel::SizeIds::medium);
            addSize (TRANS ("large"), AudiumLookAndFeel::SizeIds::large);
            addSize (TRANS ("huge"), AudiumLookAndFeel::SizeIds::huge);
            
            m.addSubMenu("Waveform Size", sizeMenu);
            
            // Toggle visibility of each analysis overlay (ticked = currently shown).
            PopupMenu showMenu;
            auto addAnalysis = [this, &showMenu, safeThis] (const String& name, audium::AnalysisType type) {
                showMenu.addItem (name, true, audioTrack->getViewState().isAnalysisTypeVisible (type), [safeThis, type] {
                    if (safeThis != nullptr)
                        safeThis->toggleAnalysisVisibility (type);
                });
            };
            addAnalysis (TRANS ("Segments"), audium::AnalysisType::Segments);
            addAnalysis (TRANS ("Onsets"), audium::AnalysisType::Onsets);
            addAnalysis (TRANS ("Beats"), audium::AnalysisType::Beats);

            m.addSubMenu("Show Analysis", showMenu);
            
            m.showMenuAsync (PopupMenu::Options().withStandardItemHeight(AudiumLookAndFeel::popupMenuItemHeight));
        }
        
        if (!e.mods.isAnyModifierKeyDown() && !isSelected()) {
            audioTrack->getSelectionManager()->deselectAll();
        }
        
        setSelected(e.mods.isCommandDown() ? !isSelected() : true);
    }
    
    bool isSelected() const
    {
        return audioTrack->isSelected();
    }
    
    void setSelected(bool bSelected)
    {
        audioTrack->setSelected(bSelected);

        // updateAll: the arrangement rows and the right panel's selected-track
        // header need to follow, not just the channel view
        audioTrack->getAudioTrackContainer().sendActionMessage(audium::updateAll);
    }

    bool keyPressed (const juce::KeyPress& key, juce::Component* originatingComponent) override
    {
        if (key.isKeyCode (juce::KeyPress::deleteKey) || key.isKeyCode (juce::KeyPress::backspaceKey)) {
            audioTrack->getAudioTrackContainer().deleteSelectedObjects();
            return true;
        }
        return false;
    }
    
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (juce::DragAndDropContainer* container = juce::DragAndDropContainer::findParentDragContainerFor(this)) {
            container->startDragging("ChannelGroupHeaderComponent", this);
        }
    }
        
    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails &dragSourceDetails) override;
    
    void updateInsertLines(const juce::DragAndDropTarget::SourceDetails &dragSourceDetails);
    
    void hideInsertLines()
    {
        insertBefore = false;
        
        repaint();
    }
    void itemDragEnter (const SourceDetails &dragSourceDetails) override
    {
        updateInsertLines(dragSourceDetails);
    }
    
    void itemDragMove (const SourceDetails &dragSourceDetails) override
    {
        updateInsertLines(dragSourceDetails);
    }
    
    void itemDragExit (const SourceDetails &dragSourceDetails) override
    {
        hideInsertLines();
    }
    
    void itemDropped (const SourceDetails &dragSourceDetails) override;
    
    bool shouldDrawDragImageWhenOver () override
    {
        return true;
    }
    std::shared_ptr<audium::AudioTrack> getAudioTrack() const { return audioTrack; }

private:
    std::shared_ptr<audium::AudioTrack> audioTrack;
    
    std::unique_ptr<AudiumLabel> audioTrackNameLabel;
    std::unique_ptr<juce::DrawableButton> minimizeButton;
    
    bool insertBefore = false;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelGroupHeaderComponent)
};
