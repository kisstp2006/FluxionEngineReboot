#include <Fluxion/Platform/Environment.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool Fluxion_Platform_GetEnvironmentVariable(const char* name, char* outBuffer, usize bufferSize)
{
    const char* value = getenv(name);
    if (!value)
    {
        return false;
    }

    usize length = strlen(value);
    if (length + 1 > bufferSize)
    {
        return false;
    }

    memcpy(outBuffer, value, length + 1);
    return true;
}

bool Fluxion_Platform_GetExecutablePath(char* outBuffer, usize bufferSize)
{
    if (bufferSize == 0)
    {
        return false;
    }

    ssize_t written = readlink("/proc/self/exe", outBuffer, bufferSize - 1);
    if (written < 0)
    {
        return false;
    }

    outBuffer[written] = '\0';
    return true;
}

bool Fluxion_Platform_GetCurrentWorkingDirectory(char* outBuffer, usize bufferSize)
{
    return getcwd(outBuffer, bufferSize) != NULL;
}
