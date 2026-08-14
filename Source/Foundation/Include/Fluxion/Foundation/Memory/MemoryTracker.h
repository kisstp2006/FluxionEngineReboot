#pragma once

#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Diagnostics/SourceLocation.h>
#include <Fluxion/Foundation/Memory/MemoryDomain.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FluxionMemoryStatistics
{
    usize allocationCount;
    usize deallocationCount;
    usize currentBytes;
    usize peakBytes;
} FluxionMemoryStatistics;

// Fires from PushDomain/PopDomain when a hook is registered (SetHook) --
// the attach point a higher layer (Core's Profiler) uses to surface
// memory-domain scope changes through the same diagnostics adapter a CPU
// zone/marker goes through, without MemoryTracker itself depending on
// Core. `location` is the call site passed to PushDomain; NULL on a pop
// (PopDomain doesn't take one -- the push already recorded it).
typedef void (*FluxionMemoryTrackerHookFn)(FluxionMemoryDomainId id, bool isPush, const FluxionSourceLocation* location, void* userData);

#if FLUXION_MEMORY_TRACKING

void Fluxion_MemoryTracker_Init(void);

// Logs a warning for any domain whose net allocations (allocationCount -
// deallocationCount, or equivalently currentBytes) are non-zero -- a
// lightweight, opt-in leak signal, not a full leak report.
void Fluxion_MemoryTracker_Shutdown(void);

bool Fluxion_MemoryTracker_RegisterDomain(const FluxionMemoryDomainDesc* desc);

// Whether Init has been called and Shutdown has not. Registering a
// domain without that asserts, deliberately -- but a module that merely
// offers statistics when a host turned the tracker on, and stays silent
// when it did not, has to be able to ask rather than find out by
// tripping the assert. Same reasoning as Fluxion_JobSystem_IsInitialized.
bool Fluxion_MemoryTracker_IsInitialized(void);

// Returns the name a domain was registered with, or NULL if id isn't
// registered.
const char* Fluxion_MemoryTracker_GetDomainName(FluxionMemoryDomainId id);

// location is the call site entering this domain scope (typically
// captured via FLUXION_MEMORY_SCOPE) -- stored on the current thread's
// scope stack, not on the domain itself, so it reflects "what code is
// currently active in this scope" rather than "where this domain was
// last entered from anywhere".
void Fluxion_MemoryTracker_PushDomain(FluxionMemoryDomainId id, FluxionSourceLocation location);
void Fluxion_MemoryTracker_PopDomain(void);

// The domain on top of the current thread's scope stack, or
// FLUXION_MEMORY_DOMAIN_ID_INVALID if the stack is empty (untracked).
FluxionMemoryDomainId Fluxion_MemoryTracker_GetCurrentDomain(void);

// The location passed to the current thread's top-of-stack PushDomain
// call, or a zeroed FluxionSourceLocation if the stack is empty.
FluxionSourceLocation Fluxion_MemoryTracker_GetCurrentLocation(void);

FluxionMemoryStatistics Fluxion_MemoryTracker_GetStatistics(FluxionMemoryDomainId id);

// Attributes an allocation/free to a domain and rolls the delta up
// through every ancestor domain too. Intended for allocator
// implementations (see TrackingAllocator.h), not typically called
// directly by game/engine code.
void Fluxion_MemoryTracker_RecordAlloc(FluxionMemoryDomainId id, usize size);
void Fluxion_MemoryTracker_RecordFree(FluxionMemoryDomainId id, usize size);

// Registration/attach-shaped, like Fluxion_Profiler_SetBackend -- call
// during startup/shutdown, not concurrently with threads already pushing/
// popping domains. Only one hook at a time.
void Fluxion_MemoryTracker_SetHook(FluxionMemoryTrackerHookFn hook, void* userData);
void Fluxion_MemoryTracker_ClearHook(void);

#else

// FLUXION_MEMORY_TRACKING is off: every call below compiles to nothing --
// a compile-time no-op, not a runtime branch, matching the zero-cost-
// when-disabled approach games engines use for their own memory tag
// trackers.
static inline void Fluxion_MemoryTracker_Init(void) {}
static inline void Fluxion_MemoryTracker_Shutdown(void) {}

static inline bool Fluxion_MemoryTracker_IsInitialized(void) { return false; }

static inline bool Fluxion_MemoryTracker_RegisterDomain(const FluxionMemoryDomainDesc* desc)
{
    FLUXION_UNUSED(desc);
    return true;
}

static inline const char* Fluxion_MemoryTracker_GetDomainName(FluxionMemoryDomainId id)
{
    FLUXION_UNUSED(id);
    return NULL;
}

static inline void Fluxion_MemoryTracker_PushDomain(FluxionMemoryDomainId id, FluxionSourceLocation location)
{
    FLUXION_UNUSED(id);
    FLUXION_UNUSED(location);
}

static inline void Fluxion_MemoryTracker_PopDomain(void) {}

static inline FluxionMemoryDomainId Fluxion_MemoryTracker_GetCurrentDomain(void)
{
    return FLUXION_MEMORY_DOMAIN_ID_INVALID;
}

static inline FluxionSourceLocation Fluxion_MemoryTracker_GetCurrentLocation(void)
{
    FluxionSourceLocation location = { NULL, NULL, 0 };
    return location;
}

static inline FluxionMemoryStatistics Fluxion_MemoryTracker_GetStatistics(FluxionMemoryDomainId id)
{
    FLUXION_UNUSED(id);
    FluxionMemoryStatistics stats = { 0, 0, 0, 0 };
    return stats;
}

static inline void Fluxion_MemoryTracker_RecordAlloc(FluxionMemoryDomainId id, usize size)
{
    FLUXION_UNUSED(id);
    FLUXION_UNUSED(size);
}

static inline void Fluxion_MemoryTracker_RecordFree(FluxionMemoryDomainId id, usize size)
{
    FLUXION_UNUSED(id);
    FLUXION_UNUSED(size);
}

static inline void Fluxion_MemoryTracker_SetHook(FluxionMemoryTrackerHookFn hook, void* userData)
{
    FLUXION_UNUSED(hook);
    FLUXION_UNUSED(userData);
}

static inline void Fluxion_MemoryTracker_ClearHook(void) {}

#endif

#ifdef __cplusplus
}
#endif
