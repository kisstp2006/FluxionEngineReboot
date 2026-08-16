// The contents of this file are subject to the Common Public Attribution
// License Version 1.0 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// https://opensource.org/license/cpal-1-0. The License is based on the
// Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
// to cover use of software over a computer network and provide for limited
// attribution for the Original Developer. In addition, Exhibit A has been
// modified to be consistent with Exhibit B.
//
// Software distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
// for the specific language governing rights and limitations under the
// License.
//
// The Original Code is Fluxion Engine.
//
// The Original Developer is not the Initial Developer and is __________. If
// left blank, the Original Developer is the Initial Developer.
//
// The Initial Developer of the Original Code is Kiss Tibor Péter. All
// portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
// All Rights Reserved.
//
// Contributor ______________________.
//
// Alternatively, the contents of this file may be used under the terms of
// the Fluxion Engine Commercial License Agreement Version 1.0, separately
// obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
// License"), in which case the provisions of the Commercial License are
// applicable instead of those above.
//
// If you wish to allow use of your version of this file only under the terms
// of the Commercial License and not to allow others to use your version of
// this file under the CPAL, indicate your decision by deleting the
// provisions above and replace them with the notice and other provisions
// required by the Commercial License. If you do not delete the provisions
// above, a recipient may use your version of this file under either the CPAL
// or the Commercial License.
//
// SPDX-License-Identifier: CPAL-1.0

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
