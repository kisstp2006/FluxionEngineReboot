#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

u64 Fluxion_Platform_GetHighResolutionTicks(void);
u64 Fluxion_Platform_GetHighResolutionFrequency(void);

static inline f64 Fluxion_Platform_TicksToSeconds(u64 ticks, u64 frequency)
{
    return (f64)ticks / (f64)frequency;
}

void Fluxion_Platform_SleepMilliseconds(u32 milliseconds);

#ifdef __cplusplus
}
#endif
