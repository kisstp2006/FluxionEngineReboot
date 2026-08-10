#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Simple status/error type for C-facing APIs. Not a generic Result<T,E> —
// C has no templates, and a byte-generic monadic type would add complexity
// without real benefit here. C++ call sites that want a value-carrying
// result are free to use std::expected directly instead.
typedef struct FluxionResult
{
    bool ok;
    i32 code;
    const char* message;
} FluxionResult;

static inline FluxionResult Fluxion_ResultOk(void)
{
    FluxionResult result;
    result.ok = true;
    result.code = 0;
    result.message = NULL;
    return result;
}

static inline FluxionResult Fluxion_ResultError(i32 code, const char* message)
{
    FluxionResult result;
    result.ok = false;
    result.code = code;
    result.message = message;
    return result;
}

#ifdef __cplusplus
}
#endif
