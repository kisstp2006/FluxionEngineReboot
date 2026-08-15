#include "TestFramework.h"

#include <Fluxion/RenderCore/Renderer/Exposure.h>

#include <math.h>

// The camera settings, against numbers a photographer would recognise.
//
// These are not arbitrary tolerances around whatever the code returns.
// Each one is a value that exists outside this engine -- the sunny-16
// rule, the halving that each stop means -- so a wrong constant in the
// relation fails here rather than turning into a scene that is
// mysteriously dark.

static bool Near(f32 value, f32 expected, f32 tolerance)
{
    const f32 difference = value - expected;
    return (difference < 0.0f ? -difference : difference) <= tolerance;
}

static void TheSunnySixteenRuleComesOutAtFifteen(TestContext* ctx)
{
    // The oldest rule in photography: on a bright sunny day, f/16 at one
    // over the film speed is a correct exposure -- and that is exposure
    // value fifteen. If this relation were wrong, every scene lit in real
    // units would be off by however wrong it was.
    TEST_CHECK(ctx, Near(Fluxion_Exposure_EV100(16.0f, 1.0f / 125.0f, 100.0f), 15.0f, 0.05f));

    // Wide open in a dim room, which is around six and a half.
    TEST_CHECK(ctx, Near(Fluxion_Exposure_EV100(1.4f, 1.0f / 60.0f, 100.0f), 6.88f, 0.05f));
}

static void EachStopIsAFactorOfTwo(TestContext* ctx)
{
    const f32 base = Fluxion_Exposure_EV100(2.8f, 1.0f / 60.0f, 100.0f);

    // Twice the time is one stop more light, so one lower exposure value.
    TEST_CHECK(ctx, Near(Fluxion_Exposure_EV100(2.8f, 1.0f / 30.0f, 100.0f), base - 1.0f, 0.001f));

    // Twice the sensitivity, likewise.
    TEST_CHECK(ctx, Near(Fluxion_Exposure_EV100(2.8f, 1.0f / 60.0f, 200.0f), base - 1.0f, 0.001f));

    // And the aperture is a diameter, so it is the SQUARE that counts:
    // one stop is a factor of the square root of two, not of two.
    TEST_CHECK(ctx, Near(Fluxion_Exposure_EV100(2.8f * 1.41421356f, 1.0f / 60.0f, 100.0f), base + 1.0f, 0.01f));
}

static void TheMultiplierHalvesAsTheExposureValueRises(TestContext* ctx)
{
    const f32 atZero = Fluxion_Exposure_FromEV100(0.0f);

    // The calibration constant, on its own: at exposure value zero the
    // multiplier is one over 1.2.
    TEST_CHECK(ctx, Near(atZero, 1.0f / 1.2f, 0.0001f));

    TEST_CHECK(ctx, Near(Fluxion_Exposure_FromEV100(1.0f), atZero * 0.5f, 0.0001f));
    TEST_CHECK(ctx, Near(Fluxion_Exposure_FromEV100(-1.0f), atZero * 2.0f, 0.0001f));

    // Bright daylight makes a very small multiplier, which is the point:
    // sunlight is enormous and a screen is not.
    TEST_CHECK(ctx, Fluxion_Exposure_FromCamera(16.0f, 1.0f / 125.0f, 100.0f) < 0.0001f);

    // And the two halves agree with the whole.
    const f32 combined = Fluxion_Exposure_FromCamera(5.6f, 1.0f / 250.0f, 400.0f);
    const f32 separately = Fluxion_Exposure_FromEV100(Fluxion_Exposure_EV100(5.6f, 1.0f / 250.0f, 400.0f));
    TEST_CHECK(ctx, Near(combined, separately, 0.000001f));
}

static void ACameraThatCannotExistIsRefused(TestContext* ctx)
{
    // Zero back, not an infinity. An aperture of zero admits no light and
    // a shutter of zero is never open -- and a division by one of those
    // would travel into every pixel of the frame as a value nothing
    // downstream could tell from a very bright one.
    TEST_CHECK(ctx, Fluxion_Exposure_FromCamera(0.0f, 1.0f / 60.0f, 100.0f) == 0.0f);
    TEST_CHECK(ctx, Fluxion_Exposure_FromCamera(2.8f, 0.0f, 100.0f) == 0.0f);
    TEST_CHECK(ctx, Fluxion_Exposure_FromCamera(2.8f, 1.0f / 60.0f, 0.0f) == 0.0f);
    TEST_CHECK(ctx, Fluxion_Exposure_FromCamera(-1.0f, 1.0f / 60.0f, 100.0f) == 0.0f);

    TEST_CHECK(ctx, Fluxion_Exposure_EV100(0.0f, 1.0f / 60.0f, 100.0f) == 0.0f);
    TEST_CHECK(ctx, Fluxion_Exposure_EV100(2.8f, -1.0f, 100.0f) == 0.0f);
}

void Test_Exposure_Run(TestContext* ctx)
{
    TheSunnySixteenRuleComesOutAtFifteen(ctx);
    EachStopIsAFactorOfTwo(ctx);
    TheMultiplierHalvesAsTheExposureValueRises(ctx);
    ACameraThatCannotExistIsRefused(ctx);
}
