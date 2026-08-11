#include <Fluxion/Platform/Semaphore.h>

#include <Fluxion/Foundation/Defines.h>

#include <semaphore.h>

static_assert(sizeof(sem_t) <= sizeof(((FluxionSemaphore*)0)->opaque),
    "FluxionSemaphore opaque storage too small for sem_t");

void Fluxion_Platform_SemaphoreInit(FluxionSemaphore* semaphore, u32 initialCount, u32 maxCount)
{
    // POSIX unnamed semaphores have no upper-bound parameter (effectively
    // capped at SEM_VALUE_MAX, far beyond anything a worker pool needs).
    FLUXION_UNUSED(maxCount);
    sem_init((sem_t*)semaphore->opaque, 0, initialCount);
}

void Fluxion_Platform_SemaphoreDestroy(FluxionSemaphore* semaphore)
{
    sem_destroy((sem_t*)semaphore->opaque);
}

void Fluxion_Platform_SemaphoreWait(FluxionSemaphore* semaphore)
{
    sem_wait((sem_t*)semaphore->opaque);
}

void Fluxion_Platform_SemaphoreSignal(FluxionSemaphore* semaphore, u32 count)
{
    for (u32 i = 0; i < count; ++i)
    {
        sem_post((sem_t*)semaphore->opaque);
    }
}
