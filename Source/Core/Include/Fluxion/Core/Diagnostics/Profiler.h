#pragma once

#include <Fluxion/Core/Diagnostics/ProfileBackend.h>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Diagnostics/SourceLocation.h>

#ifdef __cplusplus
extern "C" {
#endif

#if FLUXION_PROFILING

// backend must stay valid until ClearBackend() is called or a different
// backend replaces it -- the profiler stores the pointer, it does not
// copy the pointed-to table. Call during startup/shutdown, not
// concurrently with other threads already emitting zones/markers.
void Fluxion_Profiler_SetBackend(const FluxionProfileBackend* backend);
void Fluxion_Profiler_ClearBackend(void);

// No-ops (near-zero cost, single null check) when no backend is attached.
// There is no internal nesting stack -- the hierarchy a backend sees is
// exactly the caller's Begin/End call order, so mismatched Begin/End
// pairs are the caller's responsibility (ProfileScope.hpp's RAII pairing
// is the intended way to always get this right).
void Fluxion_Profiler_ZoneBegin(const FluxionSourceLocation* location, const char* name);
void Fluxion_Profiler_ZoneEnd(void);
void Fluxion_Profiler_Marker(const FluxionSourceLocation* location, const char* name);

// Renames the calling OS thread (via Fluxion_Platform_SetCurrentThreadName)
// and, if a backend is attached, also reports the name to it -- one call
// instead of two so callers never rename the OS thread while forgetting
// to tell the profiler backend, or vice versa.
void Fluxion_Profiler_SetThreadName(const char* name);

#else

// FLUXION_PROFILING is off: every call below compiles to nothing -- a
// compile-time no-op, not a runtime branch, matching MemoryTracker.h's
// zero-cost-when-disabled approach.
static inline void Fluxion_Profiler_SetBackend(const FluxionProfileBackend* backend) { FLUXION_UNUSED(backend); }
static inline void Fluxion_Profiler_ClearBackend(void) {}

static inline void Fluxion_Profiler_ZoneBegin(const FluxionSourceLocation* location, const char* name)
{
    FLUXION_UNUSED(location);
    FLUXION_UNUSED(name);
}

static inline void Fluxion_Profiler_ZoneEnd(void) {}

static inline void Fluxion_Profiler_Marker(const FluxionSourceLocation* location, const char* name)
{
    FLUXION_UNUSED(location);
    FLUXION_UNUSED(name);
}

// Still renames the OS thread even when profiling is compiled out --
// thread naming is useful in a debugger regardless of whether a profiler
// backend is attached, and Fluxion_Platform_SetCurrentThreadName has no
// meaningful cost to skip.
void Fluxion_Profiler_SetThreadName(const char* name);

#endif

#ifdef __cplusplus
}
#endif
