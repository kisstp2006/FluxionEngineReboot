#include <Fluxion/Platform/Memory.h>

#include <Fluxion/Foundation/Defines.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

void* Fluxion_Platform_ReserveVirtualMemory(usize size)
{
    return VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
}

bool Fluxion_Platform_CommitVirtualMemory(void* address, usize size)
{
    return VirtualAlloc(address, size, MEM_COMMIT, PAGE_READWRITE) != NULL;
}

bool Fluxion_Platform_DecommitVirtualMemory(void* address, usize size)
{
    return VirtualFree(address, size, MEM_DECOMMIT) != 0;
}

void Fluxion_Platform_ReleaseVirtualMemory(void* address, usize size)
{
    FLUXION_UNUSED(size);
    VirtualFree(address, 0, MEM_RELEASE);
}

usize Fluxion_Platform_GetPageSize(void)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (usize)info.dwPageSize;
}
