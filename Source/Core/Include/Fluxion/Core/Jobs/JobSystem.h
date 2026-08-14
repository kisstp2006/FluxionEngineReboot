#pragma once

#include <Fluxion/Core/Jobs/JobDesc.h>
#include <Fluxion/Core/Jobs/JobHandle.h>
#include <Fluxion/Foundation/Memory/ScratchAllocator.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// workerCount == 0 uses (logical processor count - 1), leaving one core
// for the calling/main thread, clamped to at least 1. singleThreaded
// forces every submitted job to run synchronously, in submission order,
// on the calling thread instead of spawning any worker threads at all --
// a deterministic debug/test mode (repeated runs produce the exact same
// order), not the shipping default.
void Fluxion_JobSystem_Init(u32 workerCount, bool singleThreaded);

// Waits for every in-flight job to finish, then stops and joins all
// worker threads. Asserts nothing is still in flight (a clean drain).
void Fluxion_JobSystem_Shutdown(void);

// Whether Init has been called and Shutdown has not. Submitting without
// that asserts, deliberately -- handing work to a system that was never
// started is a mistake, not a runtime condition to be tolerated.
//
// This exists for the caller that genuinely has both paths: work it would
// hand to a worker if there is one, and would otherwise simply do itself.
// Such a caller has to be able to ask, rather than find out by tripping
// the assert.
bool Fluxion_JobSystem_IsInitialized(void);

// Copies *desc into the job pool and makes it runnable once every
// dependency in desc->dependencies has finished (immediately, if there
// are none, or if they've already finished). Returns an invalid handle
// (FLUXION_HANDLE_IS_VALID false) if the pool is full, or -- in
// singleThreaded mode -- because the job already ran synchronously
// before Submit returned, so there's nothing left to hand out a live
// handle for.
FluxionJobHandle Fluxion_JobSystem_Submit(const FluxionJobDesc* desc);

// Blocks the calling thread until handle's job has finished (a no-op if
// handle is already invalid/stale, i.e. already done). While waiting,
// the calling thread also executes other ready jobs from the pool
// instead of idling -- so Wait() from a worker thread doesn't deadlock
// the pool, and Wait() from the main thread doesn't waste its time
// either.
void Fluxion_JobSystem_Wait(FluxionJobHandle handle);

// Builds a single handle that completes only once every handle in
// `handles` has completed -- pass it as another job's sole dependency to
// make that job wait on all of them. `count` is bounded by
// FLUXION_JOB_MAX_DEPENDENCIES (this combines a short, explicit list of
// handles the caller already has -- for combining many programmatically-
// generated handles, see ParallelFor, which does not share this limit).
FluxionJobHandle Fluxion_JobSystem_CombineDependencies(const FluxionJobHandle* handles, u32 count);

typedef void (*FluxionParallelForFn)(void* userData, u32 index);

// Splits [0, count) into chunks of at most batchSize and submits one job
// per chunk, each calling fn(userData, index) for every index in its
// chunk. Returns a handle that completes once every chunk has -- the
// chunk count is not bounded by FLUXION_JOB_MAX_DEPENDENCIES.
FluxionJobHandle Fluxion_JobSystem_ParallelFor(
    u32 count,
    u32 batchSize,
    FluxionParallelForFn fn,
    void* userData,
    const FluxionJobHandle* dependencies,
    u32 dependencyCount);

// The calling thread's own scratch allocator if it's a worker thread,
// NULL otherwise (e.g. called from the main thread, or before Init).
// Callers reset it explicitly (e.g. once per frame) only when no jobs
// are currently running -- resetting while a job might still be using it
// would invalidate live allocations.
FluxionScratchAllocator* Fluxion_JobSystem_GetWorkerScratchAllocator(void);
void Fluxion_JobSystem_ResetWorkerScratchAllocators(void);

#ifdef __cplusplus
}
#endif
