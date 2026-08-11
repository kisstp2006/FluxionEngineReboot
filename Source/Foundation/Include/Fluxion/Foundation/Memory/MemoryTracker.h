#pragma once

#include <Fluxion/Foundation/Defines.h>
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

#if FLUXION_MEMORY_TRACKING

void Fluxion_MemoryTracker_Init(void);

// Logs a warning for any domain whose net allocations (allocationCount -
// deallocationCount, or equivalently currentBytes) are non-zero -- a
// lightweight, opt-in leak signal, not a full leak report.
void Fluxion_MemoryTracker_Shutdown(void);

bool Fluxion_MemoryTracker_RegisterDomain(const FluxionMemoryDomainDesc* desc);

void Fluxion_MemoryTracker_PushDomain(FluxionMemoryDomainId id);
void Fluxion_MemoryTracker_PopDomain(void);

// The domain on top of the current thread's scope stack, or
// FLUXION_MEMORY_DOMAIN_ID_INVALID if the stack is empty (untracked).
FluxionMemoryDomainId Fluxion_MemoryTracker_GetCurrentDomain(void);

FluxionMemoryStatistics Fluxion_MemoryTracker_GetStatistics(FluxionMemoryDomainId id);

// Attributes an allocation/free to a domain and rolls the delta up
// through every ancestor domain too. Intended for allocator
// implementations (see TrackingAllocator.h), not typically called
// directly by game/engine code.
void Fluxion_MemoryTracker_RecordAlloc(FluxionMemoryDomainId id, usize size);
void Fluxion_MemoryTracker_RecordFree(FluxionMemoryDomainId id, usize size);

#else

// FLUXION_MEMORY_TRACKING is off: every call below compiles to nothing --
// a compile-time no-op, not a runtime branch, matching the zero-cost-
// when-disabled approach games engines use for their own memory tag
// trackers.
static inline void Fluxion_MemoryTracker_Init(void) {}
static inline void Fluxion_MemoryTracker_Shutdown(void) {}

static inline bool Fluxion_MemoryTracker_RegisterDomain(const FluxionMemoryDomainDesc* desc)
{
    FLUXION_UNUSED(desc);
    return true;
}

static inline void Fluxion_MemoryTracker_PushDomain(FluxionMemoryDomainId id) { FLUXION_UNUSED(id); }
static inline void Fluxion_MemoryTracker_PopDomain(void) {}

static inline FluxionMemoryDomainId Fluxion_MemoryTracker_GetCurrentDomain(void)
{
    return FLUXION_MEMORY_DOMAIN_ID_INVALID;
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

#endif

#ifdef __cplusplus
}
#endif
