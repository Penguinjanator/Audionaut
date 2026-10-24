//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include <catch2/catch_test_macros.hpp>

#include "Util/DistributionChannel.h"

using namespace audium;

SCENARIO("the distribution channel follows the App Store receipt", "[util][channel]")
{
    GIVEN("an app bundle in a scratch directory")
    {
        const auto scratch = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("AudionautChannelTest-" + juce::Uuid().toString());
        const auto bundle = scratch.getChildFile ("Audionaut.app");
        REQUIRE (bundle.getChildFile ("Contents").createDirectory());

        THEN("without a receipt it is not an App Store install")
        {
            REQUIRE_FALSE (hasAppStoreReceipt (bundle));
        }

        WHEN("the store's receipt is present")
        {
            REQUIRE (bundle.getChildFile ("Contents/_MASReceipt/receipt").create());

            THEN("it is an App Store install")
            {
                REQUIRE (hasAppStoreReceipt (bundle));
            }
        }

        WHEN("only the receipt directory exists")
        {
            REQUIRE (bundle.getChildFile ("Contents/_MASReceipt/receipt").createDirectory());

            THEN("it is not an App Store install")
            {
                REQUIRE_FALSE (hasAppStoreReceipt (bundle));
            }
        }

        scratch.deleteRecursively();
    }

    THEN("the test binary itself ships through GitHub")
    {
        REQUIRE (currentDistributionChannel() == DistributionChannel::gitHub);
    }
}
