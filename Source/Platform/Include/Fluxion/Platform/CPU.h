#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

u32 Fluxion_Platform_GetLogicalProcessorCount(void);

// Fixed 64-byte default (correct for essentially all current x86/ARM
// desktop and mobile CPUs). Precise per-CPU cache line detection can be
// added later if a system genuinely needs it.
usize Fluxion_Platform_GetCacheLineSize(void);

#ifdef __cplusplus
}
#endif
