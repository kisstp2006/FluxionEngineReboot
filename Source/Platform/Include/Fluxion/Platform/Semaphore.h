#pragma once

#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque storage sized to fit a Windows semaphore HANDLE or a POSIX
// sem_t without a heap allocation -- same pattern as FluxionMutex
// (Synchronization.h).
typedef struct FluxionSemaphore
{
    FLUXION_ALIGN(16) u8 opaque[64];
} FluxionSemaphore;

// initialCount is how many Wait() calls can succeed immediately without
// an intervening Signal(). maxCount bounds how high the count can climb
// (Windows enforces this; POSIX unnamed semaphores don't have an
// equivalent parameter, so the Linux backend ignores it).
void Fluxion_Platform_SemaphoreInit(FluxionSemaphore* semaphore, u32 initialCount, u32 maxCount);
void Fluxion_Platform_SemaphoreDestroy(FluxionSemaphore* semaphore);

// Blocks until the count is > 0, then atomically decrements it.
void Fluxion_Platform_SemaphoreWait(FluxionSemaphore* semaphore);

// Increments the count by `count`, waking up to that many waiters.
void Fluxion_Platform_SemaphoreSignal(FluxionSemaphore* semaphore, u32 count);

#ifdef __cplusplus
}
#endif
