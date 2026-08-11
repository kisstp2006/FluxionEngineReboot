#include <Fluxion/Platform/Semaphore.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

static_assert(sizeof(HANDLE) <= sizeof(((FluxionSemaphore*)0)->opaque),
    "FluxionSemaphore opaque storage too small for HANDLE");

void Fluxion_Platform_SemaphoreInit(FluxionSemaphore* semaphore, u32 initialCount, u32 maxCount)
{
    *(HANDLE*)semaphore->opaque = CreateSemaphoreW(NULL, (LONG)initialCount, (LONG)maxCount, NULL);
}

void Fluxion_Platform_SemaphoreDestroy(FluxionSemaphore* semaphore)
{
    CloseHandle(*(HANDLE*)semaphore->opaque);
}

void Fluxion_Platform_SemaphoreWait(FluxionSemaphore* semaphore)
{
    WaitForSingleObject(*(HANDLE*)semaphore->opaque, INFINITE);
}

void Fluxion_Platform_SemaphoreSignal(FluxionSemaphore* semaphore, u32 count)
{
    ReleaseSemaphore(*(HANDLE*)semaphore->opaque, (LONG)count, NULL);
}
