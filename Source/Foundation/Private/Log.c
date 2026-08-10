#include <Fluxion/Foundation/Log.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char* Fluxion_LogLevelToString(FluxionLogLevel level)
{
    switch (level)
    {
        case FLUXION_LOG_LEVEL_TRACE: return "TRACE";
        case FLUXION_LOG_LEVEL_INFO:  return "INFO";
        case FLUXION_LOG_LEVEL_WARN:  return "WARN";
        case FLUXION_LOG_LEVEL_ERROR: return "ERROR";
        case FLUXION_LOG_LEVEL_FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

// __FILE__ is typically an absolute (or build-tree-relative) path; trim it
// down to just the filename so log lines stay readable.
static const char* Fluxion_LogFileName(const char* file)
{
    if (!file)
    {
        return "?";
    }

    const char* lastSlash = strrchr(file, '/');
    const char* lastBackslash = strrchr(file, '\\');
    const char* last = lastSlash;
    if (lastBackslash && (!last || lastBackslash > last))
    {
        last = lastBackslash;
    }
    return last ? (last + 1) : file;
}

void Fluxion_Log(FluxionLogLevel level, const char* category, const char* file, int line, const char* format, ...)
{
    FILE* stream = (level >= FLUXION_LOG_LEVEL_WARN) ? stderr : stdout;

    fprintf(stream, "[%s] [%s] (%s:%d) ",
        Fluxion_LogLevelToString(level),
        category ? category : "General",
        Fluxion_LogFileName(file),
        line);

    va_list args;
    va_start(args, format);
    vfprintf(stream, format, args);
    va_end(args);

    fputc('\n', stream);
}
