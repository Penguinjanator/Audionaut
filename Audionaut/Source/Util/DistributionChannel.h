//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#pragma once

#include <JuceHeader.h>

namespace audium {

/// Where this copy of Audionaut came from - and so where it asks for updates
/// and which build a crash report names. The channels' versions diverge:
/// the App Store carries what Apple approved, GitHub Releases the latest tag.
enum class DistributionChannel
{
    appStore,   ///< Mac App Store install
    gitHub      ///< GitHub Releases: the Windows installer, Linux builds, the macOS DMG
};

/// True when @p appBundle carries a Mac App Store receipt. The store writes
/// Contents/_MASReceipt/receipt into every app it installs; Developer ID
/// builds (the DMG) and Xcode builds never have one.
inline bool hasAppStoreReceipt (const juce::File& appBundle)
{
    return appBundle.getChildFile ("Contents")
                    .getChildFile ("_MASReceipt")
                    .getChildFile ("receipt")
                    .existsAsFile();
}

/// The running app's channel, decided at runtime: on macOS by the receipt,
/// everywhere else GitHub Releases. One binary thus serves both macOS
/// channels - no build-time define to set or forget.
inline DistributionChannel currentDistributionChannel()
{
#if JUCE_MAC
    if (hasAppStoreReceipt (juce::File::getSpecialLocation (juce::File::currentApplicationFile)))
        return DistributionChannel::appStore;
#endif

    return DistributionChannel::gitHub;
}

} // namespace audium
