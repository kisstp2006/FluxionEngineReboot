#include <Fluxion/Platform/Debug.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

bool Fluxion_Platform_IsDebuggerAttached(void)
{
    return IsDebuggerPresent() != 0;
}

void Fluxion_Platform_DebugPrint(const char* message)
{
    OutputDebugStringA(message);
}
