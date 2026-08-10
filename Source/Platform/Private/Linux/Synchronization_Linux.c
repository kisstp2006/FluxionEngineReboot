#include <Fluxion/Platform/Synchronization.h>

#include <pthread.h>

static_assert(sizeof(pthread_mutex_t) <= sizeof(((FluxionMutex*)0)->opaque),
    "FluxionMutex opaque storage too small for pthread_mutex_t");

void Fluxion_Platform_MutexInit(FluxionMutex* mutex)
{
    pthread_mutex_init((pthread_mutex_t*)mutex->opaque, NULL);
}

void Fluxion_Platform_MutexDestroy(FluxionMutex* mutex)
{
    pthread_mutex_destroy((pthread_mutex_t*)mutex->opaque);
}

void Fluxion_Platform_MutexLock(FluxionMutex* mutex)
{
    pthread_mutex_lock((pthread_mutex_t*)mutex->opaque);
}

void Fluxion_Platform_MutexUnlock(FluxionMutex* mutex)
{
    pthread_mutex_unlock((pthread_mutex_t*)mutex->opaque);
}

bool Fluxion_Platform_MutexTryLock(FluxionMutex* mutex)
{
    return pthread_mutex_trylock((pthread_mutex_t*)mutex->opaque) == 0;
}
