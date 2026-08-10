#include <Fluxion/Platform/Time.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

u64 Fluxion_Platform_GetHighResolutionTicks(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (u64)counter.QuadPart;
}

u64 Fluxion_Platform_GetHighResolutionFrequency(void)
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return (u64)frequency.QuadPart;
}

void Fluxion_Platform_SleepMilliseconds(u32 milliseconds)
{
    Sleep((DWORD)milliseconds);
}
