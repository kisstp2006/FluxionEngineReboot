#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// The longest step the clock will ever report, until something says
// otherwise. A frame that really did take longer than this -- a debugger
// break, a window being dragged, a driver stall -- is reported as this
// instead, because a simulation handed the true figure moves everything
// it owns a quarter of a second or more in one step and comes apart:
// things pass through each other, integrators diverge, and none of it is
// recoverable afterwards. Running slow is the lesser wrong.
#define FLUXION_TIME_DEFAULT_MAXIMUM_DELTA 0.25f

void Fluxion_Time_Init(void);
void Fluxion_Time_Shutdown(void);

// Closes the frame that was running and opens the next one: everything
// below answers about the frame this call began, and answers the same
// thing every time it is asked until the next call. Nothing else advances
// the clock, so two parts of one frame asking how much time has passed
// are never told different numbers.
//
// The first call after Init reports no time at all, because there was no
// previous frame for any to have passed during.
void Fluxion_Time_BeginFrame(void);

// How long the previous frame took, clamped and then scaled -- this is
// what a simulation should step by.
f32 Fluxion_Time_GetDeltaTime(void);

// The same figure with the scale left out. This is what anything that
// must keep real time regardless of how fast the world is running should
// use: a menu animation, a stopwatch, a frame-rate readout.
f32 Fluxion_Time_GetUnscaledDeltaTime(void);

// Every delta so far, added up. Held as a double because a float stops
// being able to tell consecutive frames apart after a few hours of them.
f64 Fluxion_Time_GetElapsedTime(void);
f64 Fluxion_Time_GetUnscaledElapsedTime(void);

// How many frames have begun. Zero until the first Fluxion_Time_BeginFrame.
u64 Fluxion_Time_GetFrameCount(void);

// How fast the world runs relative to real time: 1 is real time, 0 stops
// it, 0.5 halves it. A negative figure is refused and taken as 0 -- time
// running backwards is not something the rest of the engine is written to
// survive.
void Fluxion_Time_SetTimeScale(f32 scale);
f32 Fluxion_Time_GetTimeScale(void);

// The ceiling described above. A negative figure is taken as 0, which
// stops the clock entirely rather than meaning anything stranger.
void Fluxion_Time_SetMaximumDeltaTime(f32 seconds);
f32 Fluxion_Time_GetMaximumDeltaTime(void);

#ifdef __cplusplus
}
#endif
