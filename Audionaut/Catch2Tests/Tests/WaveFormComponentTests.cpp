#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Engine/PlayList/ClipDynamics.h"
#include "Engine/Resource/audium_AudioThumbnail.h"

TEST_CASE( "waveform fade envelope", "[WaveFormComponent][fade]" ) {

    SECTION("fade curve is the sqrt of the linear progress, clamped to 0..1") {
        REQUIRE(audium::ClipDynamics::fadeCurve(0.0)  == 0.0);
        REQUIRE(audium::ClipDynamics::fadeCurve(0.5)  == Catch::Approx(std::sqrt(0.5)));
        REQUIRE(audium::ClipDynamics::fadeCurve(1.0)  == 1.0);
        REQUIRE(audium::ClipDynamics::fadeCurve(-0.5) == 0.0);
        REQUIRE(audium::ClipDynamics::fadeCurve(2.0)  == 1.0);
    }

    SECTION("the curve exponent bends the fade") {
        // 1 = linear, > 1 = exponential; midpoint value = 0.5^p
        REQUIRE(audium::ClipDynamics::fadeCurve(0.5, 1.0) == Catch::Approx(0.5));
        REQUIRE(audium::ClipDynamics::fadeCurve(0.5, 2.0) == Catch::Approx(0.25));
        REQUIRE(audium::ClipDynamics::fadeCurve(0.0, 2.0) == 0.0);
        REQUIRE(audium::ClipDynamics::fadeCurve(1.0, 2.0) == 1.0);
    }

    SECTION("a default envelope is inactive") {
        audium::WaveformEnvelope envelope;
        REQUIRE_FALSE(envelope.isActive());
        REQUIRE(envelope.gainAt(0.0f) == 1.0f);
    }

    SECTION("the envelope follows the fade curve within the fade spans") {
        audium::WaveformEnvelope envelope;
        envelope.fadeInWidth  = 100.0f;
        envelope.fadeOutWidth = 50.0f;
        envelope.totalWidth   = 1000.0f;

        REQUIRE(envelope.isActive());

        // fade in: 0 at the clip start, sqrt curve up to 1 at fadeInWidth
        REQUIRE(envelope.gainAt(0.0f)  == 0.0f);
        REQUIRE(envelope.gainAt(50.0f) == Catch::Approx(std::sqrt(0.5)));

        // flat middle
        REQUIRE(envelope.gainAt(100.0f) == 1.0f);
        REQUIRE(envelope.gainAt(500.0f) == 1.0f);
        REQUIRE(envelope.gainAt(950.0f) == 1.0f);

        // fade out: sqrt curve down to 0 at the clip end
        REQUIRE(envelope.gainAt(975.0f)  == Catch::Approx(std::sqrt(0.5)));
        REQUIRE(envelope.gainAt(1000.0f) == 0.0f);
    }

    SECTION("ramp offsets shift the fade spans") {
        audium::WaveformEnvelope envelope;
        envelope.fadeInWidth      = 100.0f;
        envelope.fadeInStartWidth = 50.0f;   // silent head inside the clip
        envelope.fadeOutWidth     = 100.0f;
        envelope.fadeOutEndWidth  = 50.0f;   // silent tail inside the clip
        envelope.totalWidth       = 1000.0f;

        REQUIRE(envelope.gainAt(25.0f)  == 0.0f);
        REQUIRE(envelope.gainAt(75.0f)  == Catch::Approx(std::sqrt(0.5)));
        REQUIRE(envelope.gainAt(500.0f) == 1.0f);
        REQUIRE(envelope.gainAt(925.0f) == Catch::Approx(std::sqrt(0.5)));
        REQUIRE(envelope.gainAt(975.0f) == 0.0f);
    }

    SECTION("a ramp lying entirely outside the clip still tapers negative x") {
        // fade-in end at the clip start, start dragged outside: the ghost
        // waveform (drawn at x < 0 in clip-local space) must follow the ramp
        // even though the in-clip fade width is zero
        audium::WaveformEnvelope envelope;
        envelope.fadeInWidth      = 0.0f;
        envelope.fadeInStartWidth = -100.0f;
        envelope.fadeOutWidth     = 0.0f;
        envelope.fadeOutEndWidth  = -100.0f;
        envelope.totalWidth       = 1000.0f;

        // fade-in ramp over [-100, 0]
        REQUIRE(envelope.gainAt(-100.0f) == 0.0f);
        REQUIRE(envelope.gainAt(-50.0f)  == Catch::Approx(std::sqrt(0.5)));
        REQUIRE(envelope.gainAt(0.0f)    == 1.0f);

        // inside the clip untouched
        REQUIRE(envelope.gainAt(500.0f)  == 1.0f);
        REQUIRE(envelope.gainAt(1000.0f) == 1.0f);

        // fade-out ramp over [1000, 1100]
        REQUIRE(envelope.gainAt(1050.0f) == Catch::Approx(std::sqrt(0.5)));
        REQUIRE(envelope.gainAt(1100.0f) == 0.0f);
    }

    SECTION("the envelope follows the per-ramp curve exponents") {
        audium::WaveformEnvelope envelope;
        envelope.fadeInWidth  = 100.0f;
        envelope.fadeOutWidth = 100.0f;
        envelope.fadeInCurve  = 1.0f;  // linear
        envelope.fadeOutCurve = 2.0f;  // exponential
        envelope.totalWidth   = 1000.0f;

        REQUIRE(envelope.gainAt(50.0f)  == Catch::Approx(0.5));
        REQUIRE(envelope.gainAt(950.0f) == Catch::Approx(0.25));
    }

    SECTION("a ramp crossing the clip edge tapers both sides of it") {
        audium::WaveformEnvelope envelope;
        envelope.fadeInWidth      = 100.0f;
        envelope.fadeInStartWidth = -100.0f;
        envelope.totalWidth       = 1000.0f;

        REQUIRE(envelope.gainAt(-100.0f) == 0.0f);
        REQUIRE(envelope.gainAt(0.0f)    == Catch::Approx(std::sqrt(0.5)));
        REQUIRE(envelope.gainAt(100.0f)  == 1.0f);
    }
}
