/*
  ==============================================================================

    FadeInOutView.h
    Created: 8 Feb 2025 4:12:39pm
    Author:  Klaus Voltmer

  ==============================================================================
*/

//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once

#include <JuceHeader.h>

#include "FadeInOutView.h"
#include "Engine/PlayList/PlayListItem.h"

/**
 * A fade ramp laid out for drawing - shared by FadeInOutView (the part inside
 * the clip) and ClipFadeOverlay (the continuation outside it). The ramp spans
 * [startX, startX + width]; its gain lifts the curve from bottom by up to
 * height, rising for a fade-in and falling for a fade-out.
 */
struct FadeRampGeometry
{
    float startX;
    float width;
    bool fadeOut;
    double curve;
    float bottom;
    float height;

    /// The curve's y at x.
    float yAt (float x) const;

    /// The curve between fromX and toX: a sub-path starting at (fromX, fromY),
    /// the curve sampled every 4 px (every pixel over 10 px or less) - from
    /// fromX itself, or from the first step past it when fromY already is the
    /// curve there - and a line to (toX, toY).
    juce::Path makePath (float fromX, float fromY, float toX, float toY, bool sampleFromX = true) const;

private:
    float gainAt (float offsetIntoRamp) const;
};


class FadeInOutView  : public juce::Component
{
public:
    FadeInOutView() = default;
    ~FadeInOutView() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    
    void setPlayListItem(std::shared_ptr<audium::PlayListItem> item);

private:

    std::shared_ptr<audium::PlayListItem> playListItem;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FadeInOutView)
};
