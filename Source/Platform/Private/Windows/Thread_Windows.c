#include <Fluxion/Platform/Thread.h>

#include <Fluxion/Foundation/Memory/Allocator.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

typedef struct FluxionThreadTrampolineData
{
    FluxionThreadFn fn;
    void* userData;
} FluxionThreadTrampolineData;

static DWORD WINAPI Fluxion_ThreadTrampoline(LPVOID parameter)
{
    FluxionThreadTrampolineData* data = (FluxionThreadTrampolineData*)parameter;
    data->fn(data->userData);
    Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), data, sizeof(FluxionThreadTrampolineData));
    return 0;
}

static void Fluxion_SetThreadDescriptionUtf8(HANDLE handle, const char* name)
{
    wchar_t wideName[128];
    int converted = MultiByteToWideChar(CP_UTF8, 0, name, -1, wideName, 128);
    if (converted > 0)
    {
        SetThreadDescription(handle, wideName);
    }
}

bool Fluxion_Platform_ThreadCreate(FluxionThread* thread, FluxionThreadFn fn, void* userData, const char* name)
{
    FluxionThreadTrampolineData* data = (FluxionThreadTrampolineData*)Fluxion_Allocator_Alloc(
        Fluxion_DefaultAllocator(), sizeof(FluxionThreadTrampolineData), sizeof(void*));
    data->fn = fn;
    data->userData = userData;

    HANDLE handle = CreateThread(NULL, 0, Fluxion_ThreadTrampoline, data, 0, NULL);
    if (!handle)
    {
        Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), data, sizeof(FluxionThreadTrampolineData));
        thread->handle = NULL;
        return false;
    }

    thread->handle = (void*)handle;

    if (name)
    {
        Fluxion_SetThreadDescriptionUtf8(handle, name);
    }

    return true;
}

void Fluxion_Platform_ThreadJoin(FluxionThread* thread)
{
    if (thread->handle)
    {
        WaitForSingleObject((HANDLE)thread->handle, INFINITE);
        CloseHandle((HANDLE)thread->handle);
        thread->handle = NULL;
    }
}

void Fluxion_Platform_ThreadDetach(FluxionThread* thread)
{
    if (thread->handle)
    {
        CloseHandle((HANDLE)thread->handle);
        thread->handle = NULL;
    }
}

void Fluxion_Platform_SetCurrentThreadName(const char* name)
{
    Fluxion_SetThreadDescriptionUtf8(GetCurrentThread(), name);
}

u64 Fluxion_Platform_GetCurrentThreadId(void)
{
    return (u64)GetCurrentThreadId();
}
