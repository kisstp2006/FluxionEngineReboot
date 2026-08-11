#pragma once

#include <Fluxion/Foundation/Diagnostics/SourceLocation.h>

#ifdef __cplusplus
extern "C" {
#endif

// A function-pointer adapter table -- the attach point for an external
// profiler (Tracy, PIX, RenderDoc, ...). Only one backend can be attached
// at a time (see Profiler.h); userData is passed back into every call
// unchanged so a backend can carry its own context without a global.
typedef struct FluxionProfileBackend
{
    void (*zoneBegin)(const FluxionSourceLocation* location, const char* name, void* userData);
    void (*zoneEnd)(void* userData);
    void (*marker)(const FluxionSourceLocation* location, const char* name, void* userData);
    void (*threadName)(const char* name, void* userData);
    void* userData;
} FluxionProfileBackend;

#ifdef __cplusplus
}
#endif
