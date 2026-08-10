#include <Fluxion/Platform/Debug.h>

#include <stdio.h>
#include <string.h>

bool Fluxion_Platform_IsDebuggerAttached(void)
{
    FILE* status = fopen("/proc/self/status", "r");
    if (!status)
    {
        return false;
    }

    bool attached = false;
    char line[256];
    while (fgets(line, sizeof(line), status))
    {
        if (strncmp(line, "TracerPid:", 10) == 0)
        {
            int pid = 0;
            sscanf(line + 10, "%d", &pid);
            attached = pid != 0;
            break;
        }
    }

    fclose(status);
    return attached;
}

void Fluxion_Platform_DebugPrint(const char* message)
{
    fprintf(stderr, "%s", message);
}
