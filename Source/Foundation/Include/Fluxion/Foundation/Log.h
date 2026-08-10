#pragma once

#include <Fluxion/Foundation/Defines.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FluxionLogLevel
{
    FLUXION_LOG_LEVEL_TRACE = 0,
    FLUXION_LOG_LEVEL_INFO,
    FLUXION_LOG_LEVEL_WARN,
    FLUXION_LOG_LEVEL_ERROR,
    FLUXION_LOG_LEVEL_FATAL,
} FluxionLogLevel;

// Minimal leveled logger writing to stdout/stderr. `file`/`line` identify
// the call site — always pass __FILE__/__LINE__, never call this directly;
// use the FLUXION_LOG_* macros below, which fill those in automatically so
// callers never have to think about it. NOT thread-safe yet.
void Fluxion_Log(FluxionLogLevel level, const char* category, const char* file, int line, const char* format, ...);

#ifdef __cplusplus
}
#endif

#define FLUXION_LOG_TRACE(category, ...) Fluxion_Log(FLUXION_LOG_LEVEL_TRACE, category, __FILE__, __LINE__, __VA_ARGS__)
#define FLUXION_LOG_INFO(category, ...)  Fluxion_Log(FLUXION_LOG_LEVEL_INFO,  category, __FILE__, __LINE__, __VA_ARGS__)
#define FLUXION_LOG_WARN(category, ...)  Fluxion_Log(FLUXION_LOG_LEVEL_WARN,  category, __FILE__, __LINE__, __VA_ARGS__)
#define FLUXION_LOG_ERROR(category, ...) Fluxion_Log(FLUXION_LOG_LEVEL_ERROR, category, __FILE__, __LINE__, __VA_ARGS__)
#define FLUXION_LOG_FATAL(category, ...) Fluxion_Log(FLUXION_LOG_LEVEL_FATAL, category, __FILE__, __LINE__, __VA_ARGS__)
