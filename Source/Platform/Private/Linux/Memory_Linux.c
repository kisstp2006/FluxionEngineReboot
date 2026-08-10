#include <Fluxion/Platform/Memory.h>

#include <sys/mman.h>
#include <unistd.h>

void* Fluxion_Platform_ReserveVirtualMemory(usize size)
{
    void* address = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return address == MAP_FAILED ? NULL : address;
}

bool Fluxion_Platform_CommitVirtualMemory(void* address, usize size)
{
    return mprotect(address, size, PROT_READ | PROT_WRITE) == 0;
}

bool Fluxion_Platform_DecommitVirtualMemory(void* address, usize size)
{
    return mprotect(address, size, PROT_NONE) == 0;
}

void Fluxion_Platform_ReleaseVirtualMemory(void* address, usize size)
{
    munmap(address, size);
}

usize Fluxion_Platform_GetPageSize(void)
{
    return (usize)sysconf(_SC_PAGESIZE);
}
