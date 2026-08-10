#include <Fluxion/Platform/CPU.h>

#include <unistd.h>

u32 Fluxion_Platform_GetLogicalProcessorCount(void)
{
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? (u32)count : 1u;
}

usize Fluxion_Platform_GetCacheLineSize(void)
{
    return 64;
}
