#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Plain call-site data (file/function/line) -- captured once per call site
// via FLUXION_SOURCE_LOCATION(), the same __FILE__/__LINE__ idiom
// FLUXION_LOG_* already uses, just packaged as a struct so it can be
// passed as a single pointer across a C ABI boundary instead of three
// loose parameters.
typedef struct FluxionSourceLocation
{
    const char* file;
    const char* function;
    u32 line;
} FluxionSourceLocation;

#ifdef __cplusplus
}
#endif
