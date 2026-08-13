#include "TestFramework.h"

#include <Fluxion/Application/Time/Time.h>
#include <Fluxion/Platform/Time.h>

// Nothing here asserts on a wall-clock duration: how long a sleep really
// takes is the scheduler's business and varies by an order of magnitude
// between an idle machine and a loaded one. What is checked instead is
// everything the clock promises regardless of that -- that it starts at
// nothing, that the counter goes up by one per frame, that elapsed time
// never goes backwards, and that the two figures a caller can set really
// do bound and scale what is reported.
void Test_Time_Run(TestContext* ctx)
{
    Fluxion_Time_Init();

    // Before any frame has begun there is no frame to have taken time.
    TEST_CHECK(ctx, Fluxion_Time_GetFrameCount() == 0);
    TEST_CHECK(ctx, Fluxion_Time_GetDeltaTime() == 0.0f);
    TEST_CHECK(ctx, Fluxion_Time_GetUnscaledDeltaTime() == 0.0f);
    TEST_CHECK(ctx, Fluxion_Time_GetElapsedTime() == 0.0);
    TEST_CHECK(ctx, Fluxion_Time_GetUnscaledElapsedTime() == 0.0);
    TEST_CHECK(ctx, Fluxion_Time_GetTimeScale() == 1.0f);
    TEST_CHECK(ctx, Fluxion_Time_GetMaximumDeltaTime() == FLUXION_TIME_DEFAULT_MAXIMUM_DELTA);

    // The first frame reports nothing elapsed: whatever the caller spent
    // starting up is not a frame anyone stepped over.
    Fluxion_Time_BeginFrame();
    TEST_CHECK(ctx, Fluxion_Time_GetFrameCount() == 1);
    TEST_CHECK(ctx, Fluxion_Time_GetDeltaTime() == 0.0f);
    TEST_CHECK(ctx, Fluxion_Time_GetElapsedTime() == 0.0);

    // A second frame is measured, and whatever it measures is a real
    // number of seconds that is neither negative nor past the ceiling.
    Fluxion_Platform_SleepMilliseconds(5);
    Fluxion_Time_BeginFrame();
    TEST_CHECK(ctx, Fluxion_Time_GetFrameCount() == 2);
    TEST_CHECK(ctx, Fluxion_Time_GetUnscaledDeltaTime() >= 0.0f);
    TEST_CHECK(ctx, Fluxion_Time_GetUnscaledDeltaTime() <= Fluxion_Time_GetMaximumDeltaTime());
    TEST_CHECK(ctx, Fluxion_Time_GetDeltaTime() == Fluxion_Time_GetUnscaledDeltaTime()); // scale is still 1

    // Elapsed time is exactly what the deltas added up to, and asking
    // twice in one frame answers the same thing both times.
    const f64 elapsedAfterTwo = Fluxion_Time_GetElapsedTime();
    TEST_CHECK(ctx, elapsedAfterTwo == (f64)Fluxion_Time_GetUnscaledDeltaTime());
    TEST_CHECK(ctx, Fluxion_Time_GetElapsedTime() == elapsedAfterTwo);
    TEST_CHECK(ctx, Fluxion_Time_GetDeltaTime() == Fluxion_Time_GetDeltaTime());

    // --- The ceiling ----------------------------------------------------
    //
    // A frame that really did take much longer than the ceiling is
    // reported as exactly the ceiling. The sleep is far longer than the
    // limit set here, so no amount of scheduler slack can make this land
    // anywhere but on the clamp.
    Fluxion_Time_SetMaximumDeltaTime(0.001f);
    TEST_CHECK(ctx, Fluxion_Time_GetMaximumDeltaTime() == 0.001f);

    Fluxion_Platform_SleepMilliseconds(30);
    Fluxion_Time_BeginFrame();
    TEST_CHECK(ctx, Fluxion_Time_GetUnscaledDeltaTime() == 0.001f);
    TEST_CHECK(ctx, Fluxion_Time_GetDeltaTime() == 0.001f);
    TEST_CHECK(ctx, Fluxion_Time_GetFrameCount() == 3);

    // --- The scale ------------------------------------------------------
    //
    // The scale applies to the clamped figure, so with both pinned the
    // two deltas are exactly related and neither depends on the clock.
    Fluxion_Time_SetTimeScale(0.5f);
    TEST_CHECK(ctx, Fluxion_Time_GetTimeScale() == 0.5f);

    Fluxion_Platform_SleepMilliseconds(30);
    Fluxion_Time_BeginFrame();
    TEST_CHECK(ctx, Fluxion_Time_GetUnscaledDeltaTime() == 0.001f);
    TEST_CHECK(ctx, Fluxion_Time_GetDeltaTime() == 0.0005f);

    const f64 elapsed = Fluxion_Time_GetElapsedTime();
    const f64 unscaledElapsed = Fluxion_Time_GetUnscaledElapsedTime();
    TEST_CHECK(ctx, unscaledElapsed > elapsed); // the scale has held one of them back

    // A stopped clock still counts frames, and neither elapsed figure
    // moves while it is stopped.
    Fluxion_Time_SetTimeScale(0.0f);
    Fluxion_Platform_SleepMilliseconds(5);
    Fluxion_Time_BeginFrame();
    TEST_CHECK(ctx, Fluxion_Time_GetDeltaTime() == 0.0f);
    TEST_CHECK(ctx, Fluxion_Time_GetElapsedTime() == elapsed);
    TEST_CHECK(ctx, Fluxion_Time_GetFrameCount() == 5);

    // Real time kept passing while the world was stopped, which is
    // exactly what the unscaled figure is for.
    const f64 unscaledElapsedWhileStopped = Fluxion_Time_GetUnscaledElapsedTime();
    TEST_CHECK(ctx, unscaledElapsedWhileStopped > unscaledElapsed);

    // Time running backwards is refused rather than obeyed.
    Fluxion_Time_SetTimeScale(-2.0f);
    TEST_CHECK(ctx, Fluxion_Time_GetTimeScale() == 0.0f);
    Fluxion_Time_SetMaximumDeltaTime(-1.0f);
    TEST_CHECK(ctx, Fluxion_Time_GetMaximumDeltaTime() == 0.0f);

    // Every frame from here on is nothing at all, and elapsed time holds
    // still while the frame counter keeps going.
    Fluxion_Time_SetTimeScale(1.0f);
    Fluxion_Platform_SleepMilliseconds(5);
    Fluxion_Time_BeginFrame();
    TEST_CHECK(ctx, Fluxion_Time_GetUnscaledDeltaTime() == 0.0f);
    TEST_CHECK(ctx, Fluxion_Time_GetUnscaledElapsedTime() == unscaledElapsedWhileStopped);
    TEST_CHECK(ctx, Fluxion_Time_GetFrameCount() == 6);

    Fluxion_Time_Shutdown();

    // Starting again is starting from nothing: everything a caller set is
    // back to what it was, and no time carries over.
    Fluxion_Time_Init();
    TEST_CHECK(ctx, Fluxion_Time_GetFrameCount() == 0);
    TEST_CHECK(ctx, Fluxion_Time_GetElapsedTime() == 0.0);
    TEST_CHECK(ctx, Fluxion_Time_GetTimeScale() == 1.0f);
    TEST_CHECK(ctx, Fluxion_Time_GetMaximumDeltaTime() == FLUXION_TIME_DEFAULT_MAXIMUM_DELTA);
    Fluxion_Time_Shutdown();
}
