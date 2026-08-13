#include <Fluxion/Application/Time/Time.h>

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Platform/Time.h>

// The counter the platform offers is a count of ticks since something
// unspecified, at a rate the platform also reports. Both are read once,
// here: the rate never changes while a program runs, and the first
// reading is what everything afterwards is measured from, so the numbers
// this module works with stay small enough for a double to hold every
// tick individually.
static u64 s_frequency = 0;
static u64 s_previousTicks = 0;

static f32 s_deltaTime = 0.0f;
static f32 s_unscaledDeltaTime = 0.0f;
static f64 s_elapsedTime = 0.0;
static f64 s_unscaledElapsedTime = 0.0;
static u64 s_frameCount = 0;

static f32 s_timeScale = 1.0f;
static f32 s_maximumDeltaTime = FLUXION_TIME_DEFAULT_MAXIMUM_DELTA;

static bool s_initialized = false;

void Fluxion_Time_Init(void)
{
    FLUXION_ASSERT_MSG(!s_initialized, "Fluxion_Time_Init called twice without a Shutdown in between");

    s_frequency = Fluxion_Platform_GetHighResolutionFrequency();
    FLUXION_ASSERT_MSG(s_frequency > 0, "the platform reports a high resolution counter that does not advance");

    s_previousTicks = Fluxion_Platform_GetHighResolutionTicks();

    s_deltaTime = 0.0f;
    s_unscaledDeltaTime = 0.0f;
    s_elapsedTime = 0.0;
    s_unscaledElapsedTime = 0.0;
    s_frameCount = 0;

    s_timeScale = 1.0f;
    s_maximumDeltaTime = FLUXION_TIME_DEFAULT_MAXIMUM_DELTA;

    s_initialized = true;
}

void Fluxion_Time_Shutdown(void)
{
    FLUXION_ASSERT_MSG(s_initialized, "Fluxion_Time_Shutdown called before Init");
    s_initialized = false;
}

void Fluxion_Time_BeginFrame(void)
{
    FLUXION_ASSERT(s_initialized);

    const u64 now = Fluxion_Platform_GetHighResolutionTicks();

    // The counter is monotonic, so `now` can only be behind the previous
    // reading if something outside this module moved it. Treating that as
    // no time at all is the only answer that cannot make the clock run
    // backwards.
    const u64 ticks = (now >= s_previousTicks) ? (now - s_previousTicks) : 0;
    s_previousTicks = now;

    // Nothing preceded the first frame, so nothing elapsed during it. The
    // measured figure would otherwise be however long the caller spent
    // between Init and its first frame -- startup, window creation, device
    // creation -- which is not a frame and must not be stepped over as if
    // it were.
    f32 measured = (s_frameCount == 0) ? 0.0f : (f32)Fluxion_Platform_TicksToSeconds(ticks, s_frequency);
    if (measured > s_maximumDeltaTime) measured = s_maximumDeltaTime;

    s_unscaledDeltaTime = measured;
    s_deltaTime = measured * s_timeScale;

    s_unscaledElapsedTime += (f64)s_unscaledDeltaTime;
    s_elapsedTime += (f64)s_deltaTime;

    ++s_frameCount;
}

f32 Fluxion_Time_GetDeltaTime(void) { return s_deltaTime; }
f32 Fluxion_Time_GetUnscaledDeltaTime(void) { return s_unscaledDeltaTime; }
f64 Fluxion_Time_GetElapsedTime(void) { return s_elapsedTime; }
f64 Fluxion_Time_GetUnscaledElapsedTime(void) { return s_unscaledElapsedTime; }
u64 Fluxion_Time_GetFrameCount(void) { return s_frameCount; }

void Fluxion_Time_SetTimeScale(f32 scale)
{
    s_timeScale = (scale > 0.0f) ? scale : 0.0f;
}

f32 Fluxion_Time_GetTimeScale(void) { return s_timeScale; }

void Fluxion_Time_SetMaximumDeltaTime(f32 seconds)
{
    s_maximumDeltaTime = (seconds > 0.0f) ? seconds : 0.0f;
}

f32 Fluxion_Time_GetMaximumDeltaTime(void) { return s_maximumDeltaTime; }
