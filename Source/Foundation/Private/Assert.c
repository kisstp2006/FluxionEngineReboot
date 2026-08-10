#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Log.h>

void Fluxion_AssertReport(const char* expression, const char* file, int line, const char* message)
{
    if (message)
    {
        Fluxion_Log(FLUXION_LOG_LEVEL_FATAL, "Assert", file, line, "assertion failed: %s -- %s", expression, message);
    }
    else
    {
        Fluxion_Log(FLUXION_LOG_LEVEL_FATAL, "Assert", file, line, "assertion failed: %s", expression);
    }
}
