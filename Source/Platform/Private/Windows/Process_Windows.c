#include <Fluxion/Platform/Process.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

u32 Fluxion_Platform_GetCurrentProcessId(void)
{
    return (u32)GetCurrentProcessId();
}

void Fluxion_Platform_ExitProcess(i32 exitCode)
{
    ExitProcess((UINT)exitCode);
}
