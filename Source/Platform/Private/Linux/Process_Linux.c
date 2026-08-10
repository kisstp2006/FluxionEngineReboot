#include <Fluxion/Platform/Process.h>

#include <unistd.h>

u32 Fluxion_Platform_GetCurrentProcessId(void)
{
    return (u32)getpid();
}

void Fluxion_Platform_ExitProcess(i32 exitCode)
{
    _exit(exitCode);
}
