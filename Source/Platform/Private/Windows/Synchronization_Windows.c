#include <Fluxion/Platform/Synchronization.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

static_assert(sizeof(CRITICAL_SECTION) <= sizeof(((FluxionMutex*)0)->opaque),
    "FluxionMutex opaque storage too small for CRITICAL_SECTION");

void Fluxion_Platform_MutexInit(FluxionMutex* mutex)
{
    InitializeCriticalSection((CRITICAL_SECTION*)mutex->opaque);
}

void Fluxion_Platform_MutexDestroy(FluxionMutex* mutex)
{
    DeleteCriticalSection((CRITICAL_SECTION*)mutex->opaque);
}

void Fluxion_Platform_MutexLock(FluxionMutex* mutex)
{
    EnterCriticalSection((CRITICAL_SECTION*)mutex->opaque);
}

void Fluxion_Platform_MutexUnlock(FluxionMutex* mutex)
{
    LeaveCriticalSection((CRITICAL_SECTION*)mutex->opaque);
}

bool Fluxion_Platform_MutexTryLock(FluxionMutex* mutex)
{
    return TryEnterCriticalSection((CRITICAL_SECTION*)mutex->opaque) != 0;
}
