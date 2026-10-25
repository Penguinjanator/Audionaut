//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include <JuceHeader.h>
#include "FadeInOutView.h"

using namespace juce;

float FadeRampGeometry::gainAt (float offsetIntoRamp) const
{
    auto progress = offsetIntoRamp / width;
    return static_cast<float>(audium::ClipDynamics::fadeCurve(fadeOut ? 1.f - progress : progress, curve));
}

float FadeRampGeometry::yAt (float x) const
{
    return bottom - (gainAt(x - startX) * height);
}

juce::Path FadeRampGeometry::makePath (float fromX, float fromY, float toX, float toY, bool sampleFromX) const
{
    juce::Path path;
    path.startNewSubPath(fromX, fromY);

    auto fromOffset = fromX - startX;
    auto numPixels  = static_cast<int>(toX - fromX);
    auto stride     = numPixels > 10 ? 4 : 1;
    for (auto w = sampleFromX ? 0 : stride; w < numPixels; w += stride)
        path.lineTo(fromX + static_cast<float>(w),
                    bottom - (gainAt(fromOffset + static_cast<float>(w)) * height));

    path.lineTo(toX, toY);
    return path;
}

void FadeInOutView::paint (juce::Graphics& g)
{
    if (playListItem == nullptr)
        return;

    // the curves are editing UI: unselected clips show their fades through
    // the tapered waveform alone - except the neighbour of a Shift-drag
    // crossfade, whose fade-in/out is being shaped by the drag
    if (! playListItem->isSelected() && ! playListItem->isCrossfadePartner())
        return;


    auto& dynamics      = playListItem->getDynamics();
    auto fHeight        = static_cast<float>(getHeight());
    auto fWidth         = static_cast<float>(getWidth());
    auto yOffset        = 1.f;

    // the fade-in ramp runs from fadeInStartX up to fadeInEndX, the fade-out
    // ramp from fadeOutStartX down to fadeOutEndX; audio outside
    // [fadeInStartX, fadeOutEndX] is silent. negative fractions put the ramp
    // origin outside the clip (the fade extends the audible material); the
    // off-clip part is clipped away here and drawn by the lane's
    // ClipFadeOverlay instead
    auto fFadeInStartX  = static_cast<float>(dynamics.getFadeInStart()) * fWidth;
    auto fFadeInEndX    = static_cast<float>(dynamics.getFadeIn()) * fWidth;
    auto fFadeOutStartX = fWidth - static_cast<float>(dynamics.getFadeOut()) * fWidth;
    auto fFadeOutEndX   = fWidth - static_cast<float>(dynamics.getFadeOutEnd()) * fWidth;

    // fadeInStart <= fadeIn and fadeOutEnd <= fadeOut, so a non-zero offset
    // implies a non-zero fade
    auto hasFadeIn      = static_cast<int>(fFadeInEndX) > 0;
    auto hasFadeOut     = static_cast<int>(fWidth - fFadeOutStartX) > 0;

    // dim the silent head/tail
    g.setColour(Colours::black.withAlpha(0.35f));
    if (fFadeInStartX >= 1.f)
        g.fillRect(0.f, 0.f, fFadeInStartX, fHeight);
    if (fWidth - fFadeOutEndX >= 1.f)
        g.fillRect(fFadeOutEndX, 0.f, fWidth - fFadeOutEndX, fHeight);

    // only the two ramp curves are drawn - no plateau or clip-edge lines.
    // the area OUTSIDE each curve (the attenuated part, above the ramp) is
    // darkened a little. the bend handle draws its own circle on top.
    juce::Path fillPath;
    juce::Path strokePath;

    if (hasFadeIn) {
        FadeRampGeometry ramp { fFadeInStartX, fFadeInEndX - fFadeInStartX, false,
                                dynamics.getFadeInCurve(), fHeight, fHeight };
        auto curve = ramp.makePath(fFadeInStartX, fHeight, fFadeInEndX, yOffset);

        strokePath.addPath(curve);

        // the wedge ABOVE the curve (fill only)
        curve.lineTo(fFadeInStartX, 0.f);
        curve.closeSubPath();
        fillPath.addPath(curve);
    }

    if (hasFadeOut) {
        FadeRampGeometry ramp { fFadeOutStartX, fFadeOutEndX - fFadeOutStartX, true,
                                dynamics.getFadeOutCurve(), fHeight, fHeight };
        auto curve = ramp.makePath(fFadeOutStartX, yOffset, fFadeOutEndX, fHeight);

        strokePath.addPath(curve);

        // the wedge ABOVE the curve (fill only)
        curve.lineTo(fFadeOutEndX, 0.f);
        curve.closeSubPath();
        fillPath.addPath(curve);
    }


    if (hasFadeIn ||
        hasFadeOut) {

        g.setColour(Colours::black.withAlpha(0.18f));
        g.fillPath(fillPath.createPathWithRoundedCorners(4));

        g.setColour(Colour(Colours::white).withAlpha(0.5f));
        g.strokePath(strokePath.createPathWithRoundedCorners(4), PathStrokeType (2.f));
    }

}

void FadeInOutView::resized()
{
}

void FadeInOutView::setPlayListItem(std::shared_ptr<audium::PlayListItem> item)
{
    playListItem = item;
}
