#pragma once

#include <Fluxion/Core/Jobs/JobHandle.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bounds the job pool (JobSystem.c) -- zero heap allocation, same
// rationale as FLUXION_MAX_SUBSYSTEMS/FLUXION_MAX_SERVICES/
// FLUXION_MAX_MEMORY_DOMAINS.
#define FLUXION_MAX_INFLIGHT_JOBS 1024

// A job's captured data must fit here -- Submit() copies it in by value,
// no heap allocation. The C++ facade (Jobs.hpp) enforces this at compile
// time via static_assert(sizeof(F) <= FLUXION_JOB_INLINE_DATA_SIZE).
#define FLUXION_JOB_INLINE_DATA_SIZE 48

#define FLUXION_JOB_MAX_DEPENDENCIES 8

typedef void (*FluxionJobFn)(void* data);

// `dependencies` points at caller-owned storage (typically a local
// array) -- Fluxion_JobSystem_Submit copies its contents (up to
// FLUXION_JOB_MAX_DEPENDENCIES entries) into the job pool's own slot, so
// the caller's array only needs to outlive the Submit call itself, same
// convention as FluxionSubsystemDesc.
typedef struct FluxionJobDesc
{
    FluxionJobFn function;
    u8 data[FLUXION_JOB_INLINE_DATA_SIZE];
    usize dataSize;

    const FluxionJobHandle* dependencies;
    u32 dependencyCount;
} FluxionJobDesc;

#ifdef __cplusplus
}
#endif
