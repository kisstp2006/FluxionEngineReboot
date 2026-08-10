#include <Fluxion/Platform/Environment.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

bool Fluxion_Platform_GetEnvironmentVariable(const char* name, char* outBuffer, usize bufferSize)
{
    DWORD written = GetEnvironmentVariableA(name, outBuffer, (DWORD)bufferSize);
    return written > 0 && written < (DWORD)bufferSize;
}

bool Fluxion_Platform_GetExecutablePath(char* outBuffer, usize bufferSize)
{
    DWORD written = GetModuleFileNameA(NULL, outBuffer, (DWORD)bufferSize);
    return written > 0 && written < (DWORD)bufferSize;
}

bool Fluxion_Platform_GetCurrentWorkingDirectory(char* outBuffer, usize bufferSize)
{
    DWORD written = GetCurrentDirectoryA((DWORD)bufferSize, outBuffer);
    return written > 0 && written < (DWORD)bufferSize;
}
