// _GNU_SOURCE (needed for pthread_setname_np, a glibc extension) is
// defined project-wide for this target — see Source/Platform/CMakeLists.txt.

#include <Fluxion/Platform/Thread.h>

#include <Fluxion/Foundation/Memory/Allocator.h>

#include <pthread.h>

typedef struct FluxionThreadTrampolineData
{
    FluxionThreadFn fn;
    void* userData;
} FluxionThreadTrampolineData;

static void* Fluxion_ThreadTrampoline(void* parameter)
{
    FluxionThreadTrampolineData* data = (FluxionThreadTrampolineData*)parameter;
    data->fn(data->userData);
    Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), data, sizeof(FluxionThreadTrampolineData));
    return NULL;
}

bool Fluxion_Platform_ThreadCreate(FluxionThread* thread, FluxionThreadFn fn, void* userData, const char* name)
{
    FluxionThreadTrampolineData* data = (FluxionThreadTrampolineData*)Fluxion_Allocator_Alloc(
        Fluxion_DefaultAllocator(), sizeof(FluxionThreadTrampolineData), sizeof(void*));
    data->fn = fn;
    data->userData = userData;

    pthread_t* handle = (pthread_t*)Fluxion_Allocator_Alloc(Fluxion_DefaultAllocator(), sizeof(pthread_t), sizeof(void*));

    int result = pthread_create(handle, NULL, Fluxion_ThreadTrampoline, data);
    if (result != 0)
    {
        Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), data, sizeof(FluxionThreadTrampolineData));
        Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), handle, sizeof(pthread_t));
        thread->handle = NULL;
        return false;
    }

    if (name)
    {
        // glibc caps thread names at 16 bytes including the terminator;
        // best-effort, same as the Windows backend.
        pthread_setname_np(*handle, name);
    }

    thread->handle = (void*)handle;
    return true;
}

void Fluxion_Platform_ThreadJoin(FluxionThread* thread)
{
    if (thread->handle)
    {
        pthread_t* handle = (pthread_t*)thread->handle;
        pthread_join(*handle, NULL);
        Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), handle, sizeof(pthread_t));
        thread->handle = NULL;
    }
}

void Fluxion_Platform_ThreadDetach(FluxionThread* thread)
{
    if (thread->handle)
    {
        pthread_t* handle = (pthread_t*)thread->handle;
        pthread_detach(*handle);
        Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), handle, sizeof(pthread_t));
        thread->handle = NULL;
    }
}

void Fluxion_Platform_SetCurrentThreadName(const char* name)
{
    pthread_setname_np(pthread_self(), name);
}

u64 Fluxion_Platform_GetCurrentThreadId(void)
{
    return (u64)pthread_self();
}
