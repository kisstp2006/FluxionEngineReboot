#include <Fluxion/Platform/CPU.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

u32 Fluxion_Platform_GetLogicalProcessorCount(void)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (u32)info.dwNumberOfProcessors;
}

usize Fluxion_Platform_GetCacheLineSize(void)
{
    return 64;
}
