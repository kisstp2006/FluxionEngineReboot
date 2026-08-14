#include <Fluxion/Core/Jobs/JobSystem.h>

#include <Fluxion/Core/Diagnostics/Profiler.h>
#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Atomic.h>
#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Platform/CPU.h>
#include <Fluxion/Platform/Semaphore.h>
#include <Fluxion/Platform/Synchronization.h>
#include <Fluxion/Platform/Thread.h>
#include <Fluxion/Platform/Time.h>

#include <stdio.h>
#include <string.h>

#define FLUXION_JOB_MAX_WORKERS 64
#define FLUXION_JOB_WORKER_SCRATCH_SIZE (1u * 1024u * 1024u) // 1 MiB per worker

typedef struct FluxionJobSlot
{
    FluxionJobFn function;
    u8 data[FLUXION_JOB_INLINE_DATA_SIZE];

    FluxionAtomicI32 pendingDependencyCount;

    // Ordinary job-to-job dependents: small, bounded fan-out (jobs that
    // named THIS job as one of their FLUXION_JOB_MAX_DEPENDENCIES-bounded
    // dependencies).
    FluxionJobHandle dependents[FLUXION_JOB_MAX_DEPENDENCIES];
    u32 dependentCount;

    // ParallelFor-only: if set, decrement this target's own
    // pendingDependencyCount directly when I finish, bypassing the small
    // dependents list above -- lets a barrier job depend on an
    // arbitrarily large chunk count without needing a matching-size
    // dependents array anywhere.
    bool hasDirectNotifyTarget;
    FluxionJobHandle directNotifyTarget;

    FluxionAtomicI32 finished;
    u32 generation;
} FluxionJobSlot;

typedef struct FluxionWorker
{
    u32 index;
    FluxionThread thread;

    // Fixed-capacity ready-to-run list, all pushes/pops (by the owner or
    // by a thief) under queueMutex -- sized to the whole pool because at
    // any instant a job occupies at most one worker's queue, so the sum
    // across all workers can never exceed FLUXION_MAX_INFLIGHT_JOBS.
    u32 queue[FLUXION_MAX_INFLIGHT_JOBS];
    u32 queueCount;
    FluxionMutex queueMutex;

    FluxionScratchAllocator scratch;
} FluxionWorker;

static FluxionJobSlot s_slots[FLUXION_MAX_INFLIGHT_JOBS];
static u32 s_freeSlotIndices[FLUXION_MAX_INFLIGHT_JOBS];
static u32 s_freeSlotCount;
static FluxionMutex s_poolMutex;

static FluxionWorker s_workers[FLUXION_JOB_MAX_WORKERS];
static u32 s_workerCount;
static FluxionSemaphore s_workAvailable;
static FluxionAtomicI32 s_shuttingDown;
static FluxionAtomicI32 s_nextWorkerForRoundRobin;

static bool s_singleThreaded;
static bool s_jobSystemInitialized = false;

static FLUXION_THREAD_LOCAL i32 t_workerIndex = -1;
static FLUXION_THREAD_LOCAL FluxionScratchAllocator* t_workerScratch = NULL;

static FluxionJobHandle Fluxion_MakeInvalidJobHandle(void)
{
    FluxionJobHandle handle;
    handle.index = FLUXION_HANDLE_INVALID_INDEX;
    handle.generation = 0;
    return handle;
}

static void Fluxion_NoOpJobFn(void* data)
{
    FLUXION_UNUSED(data);
}

// --- Pool bookkeeping (s_poolMutex held by every caller) -------------------

static u32 Fluxion_AllocateSlotLocked(void)
{
    if (s_freeSlotCount == 0) return FLUXION_HANDLE_INVALID_INDEX;
    return s_freeSlotIndices[--s_freeSlotCount];
}

static void Fluxion_FreeSlotLocked(u32 index)
{
    s_slots[index].generation++;
    s_freeSlotIndices[s_freeSlotCount++] = index;
}

// --- Worker queues (never held at the same time as s_poolMutex) ------------

static void Fluxion_MakeJobReady(u32 jobIndex)
{
    u32 workerIndex = (u32)Fluxion_AtomicI32_Increment(&s_nextWorkerForRoundRobin) % s_workerCount;
    FluxionWorker* worker = &s_workers[workerIndex];

    Fluxion_Platform_MutexLock(&worker->queueMutex);
    FLUXION_ASSERT_MSG(worker->queueCount < FLUXION_MAX_INFLIGHT_JOBS, "job system worker ready-queue overflow");
    worker->queue[worker->queueCount++] = jobIndex;
    Fluxion_Platform_MutexUnlock(&worker->queueMutex);

    Fluxion_Platform_SemaphoreSignal(&s_workAvailable, 1);
}

static bool Fluxion_TryPopFromQueue(u32 workerIndex, u32* outJobIndex)
{
    FluxionWorker* worker = &s_workers[workerIndex];
    bool found = false;

    Fluxion_Platform_MutexLock(&worker->queueMutex);
    if (worker->queueCount > 0)
    {
        *outJobIndex = worker->queue[--worker->queueCount];
        found = true;
    }
    Fluxion_Platform_MutexUnlock(&worker->queueMutex);

    return found;
}

static bool Fluxion_TryPopOwnThenSteal(u32 myIndex, u32* outJobIndex)
{
    if (Fluxion_TryPopFromQueue(myIndex, outJobIndex)) return true;

    for (u32 i = 0; i < s_workerCount; ++i)
    {
        if (i == myIndex) continue;
        if (Fluxion_TryPopFromQueue(i, outJobIndex)) return true;
    }
    return false;
}

static bool Fluxion_TryPopAnyReadyJob(u32* outJobIndex)
{
    for (u32 i = 0; i < s_workerCount; ++i)
    {
        if (Fluxion_TryPopFromQueue(i, outJobIndex)) return true;
    }
    return false;
}

// --- Running a job and propagating its completion ---------------------------

static void Fluxion_CompleteJob(u32 index)
{
    FluxionJobSlot* slot = &s_slots[index];
    Fluxion_AtomicI32_Store(&slot->finished, 1);

    Fluxion_Platform_MutexLock(&s_poolMutex);

    u32 readyIndices[FLUXION_JOB_MAX_DEPENDENCIES];
    u32 readyCount = 0;

    for (u32 i = 0; i < slot->dependentCount; ++i)
    {
        FluxionJobHandle dependent = slot->dependents[i];
        FluxionJobSlot* depSlot = &s_slots[dependent.index];
        if (depSlot->generation != dependent.generation) continue;

        i32 remaining = Fluxion_AtomicI32_Decrement(&depSlot->pendingDependencyCount);
        if (remaining == 0)
        {
            readyIndices[readyCount++] = dependent.index;
        }
    }

    bool barrierReady = false;
    u32 barrierIndex = 0;
    if (slot->hasDirectNotifyTarget)
    {
        FluxionJobHandle target = slot->directNotifyTarget;
        FluxionJobSlot* targetSlot = &s_slots[target.index];
        if (targetSlot->generation == target.generation)
        {
            i32 remaining = Fluxion_AtomicI32_Decrement(&targetSlot->pendingDependencyCount);
            if (remaining == 0)
            {
                barrierReady = true;
                barrierIndex = target.index;
            }
        }
    }

    Fluxion_FreeSlotLocked(index);

    Fluxion_Platform_MutexUnlock(&s_poolMutex);

    for (u32 i = 0; i < readyCount; ++i)
    {
        Fluxion_MakeJobReady(readyIndices[i]);
    }
    if (barrierReady)
    {
        Fluxion_MakeJobReady(barrierIndex);
    }
}

static void Fluxion_RunJobAndComplete(u32 jobIndex)
{
    // Safe to read function/data without s_poolMutex: once popped, this
    // job is owned exclusively by the calling thread until CompleteJob
    // notifies dependents and frees the slot below.
    FluxionJobSlot* slot = &s_slots[jobIndex];
    slot->function(slot->data);
    Fluxion_CompleteJob(jobIndex);
}

// --- Submission --------------------------------------------------------------

static FluxionJobHandle Fluxion_SubmitInternal(
    FluxionJobFn function,
    const void* data,
    usize dataSize,
    const FluxionJobHandle* dependencies,
    u32 dependencyCount,
    bool hasDirectNotifyTarget,
    FluxionJobHandle directNotifyTarget)
{
    FLUXION_ASSERT(s_jobSystemInitialized);
    FLUXION_ASSERT(dataSize <= FLUXION_JOB_INLINE_DATA_SIZE);
    FLUXION_ASSERT(dependencyCount <= FLUXION_JOB_MAX_DEPENDENCIES);

    if (s_singleThreaded)
    {
        // Every earlier handle already ran synchronously by the time this
        // call happens, so there's nothing left to actually wait on --
        // run immediately, in submission order, same call path as Init's
        // documented determinism contract.
        u8 localData[FLUXION_JOB_INLINE_DATA_SIZE];
        if (dataSize > 0)
        {
            memcpy(localData, data, dataSize);
        }
        function(localData);
        return Fluxion_MakeInvalidJobHandle();
    }

    Fluxion_Platform_MutexLock(&s_poolMutex);

    u32 index = Fluxion_AllocateSlotLocked();
    if (index == FLUXION_HANDLE_INVALID_INDEX)
    {
        Fluxion_Platform_MutexUnlock(&s_poolMutex);
        return Fluxion_MakeInvalidJobHandle();
    }

    FluxionJobSlot* slot = &s_slots[index];
    slot->function = function;
    if (dataSize > 0)
    {
        memcpy(slot->data, data, dataSize);
    }
    slot->dependentCount = 0;
    slot->hasDirectNotifyTarget = hasDirectNotifyTarget;
    slot->directNotifyTarget = directNotifyTarget;
    Fluxion_AtomicI32_Store(&slot->finished, 0);

    i32 pendingCount = 0;
    for (u32 i = 0; i < dependencyCount; ++i)
    {
        FluxionJobHandle dep = dependencies[i];
        if (!FLUXION_HANDLE_IS_VALID(dep)) continue;

        FluxionJobSlot* depSlot = &s_slots[dep.index];
        if (depSlot->generation != dep.generation) continue; // stale -> already finished
        if (Fluxion_AtomicI32_Load(&depSlot->finished)) continue; // already finished

        FLUXION_ASSERT_MSG(depSlot->dependentCount < FLUXION_JOB_MAX_DEPENDENCIES,
            "too many jobs depend on a single job -- for large fan-in, use ParallelFor's barrier pattern instead");
        if (depSlot->dependentCount < FLUXION_JOB_MAX_DEPENDENCIES)
        {
            FluxionJobHandle selfHandle;
            selfHandle.index = index;
            selfHandle.generation = slot->generation;
            depSlot->dependents[depSlot->dependentCount++] = selfHandle;
            ++pendingCount;
        }
    }

    Fluxion_AtomicI32_Store(&slot->pendingDependencyCount, pendingCount);

    FluxionJobHandle handle;
    handle.index = index;
    handle.generation = slot->generation;

    bool ready = (pendingCount == 0);
    Fluxion_Platform_MutexUnlock(&s_poolMutex);

    if (ready)
    {
        Fluxion_MakeJobReady(index);
    }

    return handle;
}

static FluxionJobHandle Fluxion_SubmitBarrier(u32 dependencyCount)
{
    FLUXION_ASSERT(s_jobSystemInitialized && !s_singleThreaded);

    Fluxion_Platform_MutexLock(&s_poolMutex);

    u32 index = Fluxion_AllocateSlotLocked();
    if (index == FLUXION_HANDLE_INVALID_INDEX)
    {
        Fluxion_Platform_MutexUnlock(&s_poolMutex);
        return Fluxion_MakeInvalidJobHandle();
    }

    FluxionJobSlot* slot = &s_slots[index];
    slot->function = Fluxion_NoOpJobFn;
    slot->dependentCount = 0;
    slot->hasDirectNotifyTarget = false;
    Fluxion_AtomicI32_Store(&slot->finished, 0);
    Fluxion_AtomicI32_Store(&slot->pendingDependencyCount, (i32)dependencyCount);

    FluxionJobHandle handle;
    handle.index = index;
    handle.generation = slot->generation;

    bool ready = (dependencyCount == 0);
    Fluxion_Platform_MutexUnlock(&s_poolMutex);

    if (ready)
    {
        Fluxion_MakeJobReady(index);
    }

    return handle;
}

// --- Worker thread loop -------------------------------------------------------

static void Fluxion_JobWorkerMain(void* userData)
{
    FluxionWorker* self = (FluxionWorker*)userData;
    t_workerIndex = (i32)self->index;
    t_workerScratch = &self->scratch;

    char name[32];
    snprintf(name, sizeof(name), "FluxionWorker%u", self->index);
    Fluxion_Profiler_SetThreadName(name);

    for (;;)
    {
        u32 jobIndex;
        if (Fluxion_TryPopOwnThenSteal(self->index, &jobIndex))
        {
            // Prefer draining real work over checking for shutdown --
            // this is what makes Shutdown() a clean drain instead of an
            // abrupt stop that could strand queued jobs.
            Fluxion_RunJobAndComplete(jobIndex);
            continue;
        }

        if (Fluxion_AtomicI32_Load(&s_shuttingDown)) break;

        Fluxion_Platform_SemaphoreWait(&s_workAvailable);
    }
}

// --- Public API ----------------------------------------------------------------

void Fluxion_JobSystem_Init(u32 workerCount, bool singleThreaded)
{
    FLUXION_ASSERT_MSG(!s_jobSystemInitialized, "Fluxion_JobSystem_Init called twice without a Shutdown in between");

    s_singleThreaded = singleThreaded;
    Fluxion_Platform_MutexInit(&s_poolMutex);

    for (u32 i = 0; i < FLUXION_MAX_INFLIGHT_JOBS; ++i)
    {
        s_slots[i].generation = 0;
        s_freeSlotIndices[i] = i;
    }
    s_freeSlotCount = FLUXION_MAX_INFLIGHT_JOBS;

    Fluxion_AtomicI32_Store(&s_shuttingDown, 0);
    Fluxion_AtomicI32_Store(&s_nextWorkerForRoundRobin, -1); // first increment yields 0

    if (singleThreaded)
    {
        s_workerCount = 0;
    }
    else
    {
        u32 resolvedCount = workerCount;
        if (resolvedCount == 0)
        {
            u32 logical = Fluxion_Platform_GetLogicalProcessorCount();
            resolvedCount = (logical > 1) ? (logical - 1) : 1;
        }
        if (resolvedCount > FLUXION_JOB_MAX_WORKERS)
        {
            resolvedCount = FLUXION_JOB_MAX_WORKERS;
        }
        s_workerCount = resolvedCount;

        Fluxion_Platform_SemaphoreInit(&s_workAvailable, 0, FLUXION_MAX_INFLIGHT_JOBS);

        for (u32 i = 0; i < s_workerCount; ++i)
        {
            s_workers[i].index = i;
            s_workers[i].queueCount = 0;
            Fluxion_Platform_MutexInit(&s_workers[i].queueMutex);
            Fluxion_ScratchAllocator_Init(&s_workers[i].scratch, Fluxion_DefaultAllocator(), FLUXION_JOB_WORKER_SCRATCH_SIZE);
        }

        for (u32 i = 0; i < s_workerCount; ++i)
        {
            bool created = Fluxion_Platform_ThreadCreate(&s_workers[i].thread, Fluxion_JobWorkerMain, &s_workers[i], NULL);
            FLUXION_ASSERT_MSG(created, "failed to create a job system worker thread");
        }
    }

    s_jobSystemInitialized = true;
}

bool Fluxion_JobSystem_IsInitialized(void)
{
    return s_jobSystemInitialized;
}

void Fluxion_JobSystem_Shutdown(void)
{
    FLUXION_ASSERT_MSG(s_jobSystemInitialized, "Fluxion_JobSystem_Shutdown called before Init");

    if (!s_singleThreaded)
    {
        Fluxion_AtomicI32_Store(&s_shuttingDown, 1);
        Fluxion_Platform_SemaphoreSignal(&s_workAvailable, s_workerCount);

        for (u32 i = 0; i < s_workerCount; ++i)
        {
            Fluxion_Platform_ThreadJoin(&s_workers[i].thread);
        }

        FLUXION_ASSERT_MSG(s_freeSlotCount == FLUXION_MAX_INFLIGHT_JOBS,
            "Fluxion_JobSystem_Shutdown: jobs still in flight (a submitted job's dependency was never itself submitted/completed)");

        for (u32 i = 0; i < s_workerCount; ++i)
        {
            Fluxion_Platform_MutexDestroy(&s_workers[i].queueMutex);
            Fluxion_ScratchAllocator_Destroy(&s_workers[i].scratch);
        }

        Fluxion_Platform_SemaphoreDestroy(&s_workAvailable);
    }

    Fluxion_Platform_MutexDestroy(&s_poolMutex);
    s_workerCount = 0;
    s_jobSystemInitialized = false;
}

FluxionJobHandle Fluxion_JobSystem_Submit(const FluxionJobDesc* desc)
{
    return Fluxion_SubmitInternal(desc->function, desc->data, desc->dataSize,
        desc->dependencies, desc->dependencyCount, false, Fluxion_MakeInvalidJobHandle());
}

void Fluxion_JobSystem_Wait(FluxionJobHandle handle)
{
    if (!FLUXION_HANDLE_IS_VALID(handle)) return;
    if (s_singleThreaded) return; // already ran synchronously before Submit returned

    for (;;)
    {
        Fluxion_Platform_MutexLock(&s_poolMutex);
        bool done = (s_slots[handle.index].generation != handle.generation) ||
                    (Fluxion_AtomicI32_Load(&s_slots[handle.index].finished) != 0);
        Fluxion_Platform_MutexUnlock(&s_poolMutex);

        if (done) return;

        u32 jobIndex;
        if (Fluxion_TryPopAnyReadyJob(&jobIndex))
        {
            Fluxion_RunJobAndComplete(jobIndex);
        }
        else
        {
            Fluxion_Platform_SleepMilliseconds(0);
        }
    }
}

FluxionJobHandle Fluxion_JobSystem_CombineDependencies(const FluxionJobHandle* handles, u32 count)
{
    if (count == 0)
    {
        return Fluxion_MakeInvalidJobHandle();
    }
    return Fluxion_SubmitInternal(Fluxion_NoOpJobFn, NULL, 0, handles, count, false, Fluxion_MakeInvalidJobHandle());
}

typedef struct FluxionParallelForChunkData
{
    FluxionParallelForFn fn;
    void* userData;
    u32 startIndex;
    u32 endIndex; // exclusive
} FluxionParallelForChunkData;

static void Fluxion_ParallelForChunkTrampoline(void* data)
{
    FluxionParallelForChunkData* chunk = (FluxionParallelForChunkData*)data;
    for (u32 i = chunk->startIndex; i < chunk->endIndex; ++i)
    {
        chunk->fn(chunk->userData, i);
    }
}

FluxionJobHandle Fluxion_JobSystem_ParallelFor(
    u32 count,
    u32 batchSize,
    FluxionParallelForFn fn,
    void* userData,
    const FluxionJobHandle* dependencies,
    u32 dependencyCount)
{
    if (count == 0)
    {
        return Fluxion_MakeInvalidJobHandle();
    }
    if (batchSize == 0)
    {
        batchSize = 1;
    }

    if (s_singleThreaded)
    {
        for (u32 i = 0; i < count; ++i)
        {
            fn(userData, i);
        }
        return Fluxion_MakeInvalidJobHandle();
    }

    u32 chunkCount = (count + batchSize - 1) / batchSize;
    FluxionJobHandle barrier = Fluxion_SubmitBarrier(chunkCount);

    for (u32 c = 0; c < chunkCount; ++c)
    {
        FluxionParallelForChunkData chunkData;
        chunkData.fn = fn;
        chunkData.userData = userData;
        chunkData.startIndex = c * batchSize;
        u32 end = chunkData.startIndex + batchSize;
        chunkData.endIndex = (end < count) ? end : count;

        Fluxion_SubmitInternal(Fluxion_ParallelForChunkTrampoline, &chunkData, sizeof(chunkData),
            dependencies, dependencyCount, true, barrier);
    }

    return barrier;
}

FluxionScratchAllocator* Fluxion_JobSystem_GetWorkerScratchAllocator(void)
{
    return t_workerScratch;
}

void Fluxion_JobSystem_ResetWorkerScratchAllocators(void)
{
    for (u32 i = 0; i < s_workerCount; ++i)
    {
        Fluxion_ScratchAllocator_Reset(&s_workers[i].scratch);
    }
}
