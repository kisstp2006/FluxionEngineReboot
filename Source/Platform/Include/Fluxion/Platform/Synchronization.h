#pragma once

#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque storage sized to fit CRITICAL_SECTION (Windows) or pthread_mutex_t
// (Linux) without a heap allocation. Not recursive. Explicitly aligned —
// both backing types contain pointer/int-sized fields that need more than
// the byte-array's default 1-byte alignment on strict-alignment targets.
// (alignas goes on the member, not after `struct`: the latter is valid
// C++ class-head syntax but not valid C — MSVC's C front end rejects it.)
typedef struct FluxionMutex
{
    FLUXION_ALIGN(16) u8 opaque[64];
} FluxionMutex;

void Fluxion_Platform_MutexInit(FluxionMutex* mutex);
void Fluxion_Platform_MutexDestroy(FluxionMutex* mutex);
void Fluxion_Platform_MutexLock(FluxionMutex* mutex);
void Fluxion_Platform_MutexUnlock(FluxionMutex* mutex);
bool Fluxion_Platform_MutexTryLock(FluxionMutex* mutex);

#ifdef __cplusplus
}
#endif
