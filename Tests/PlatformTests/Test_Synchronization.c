#include "TestFramework.h"

#include <Fluxion/Platform/Synchronization.h>
#include <Fluxion/Platform/Thread.h>

typedef struct SharedCounter
{
    FluxionMutex mutex;
    i32 value;
} SharedCounter;

static void IncrementWorker(void* userData)
{
    SharedCounter* shared = (SharedCounter*)userData;
    for (int i = 0; i < 1000; ++i)
    {
        Fluxion_Platform_MutexLock(&shared->mutex);
        shared->value += 1;
        Fluxion_Platform_MutexUnlock(&shared->mutex);
    }
}

void Test_Synchronization_Run(TestContext* ctx)
{
    SharedCounter shared;
    shared.value = 0;
    Fluxion_Platform_MutexInit(&shared.mutex);

    TEST_CHECK(ctx, Fluxion_Platform_MutexTryLock(&shared.mutex));
    Fluxion_Platform_MutexUnlock(&shared.mutex);

    // 4 threads x 1000 increments each: only correct if the mutex actually
    // provides mutual exclusion, not just a smoke test.
    FluxionThread threads[4];
    for (int i = 0; i < 4; ++i)
    {
        TEST_CHECK(ctx, Fluxion_Platform_ThreadCreate(&threads[i], IncrementWorker, &shared, NULL));
    }
    for (int i = 0; i < 4; ++i)
    {
        Fluxion_Platform_ThreadJoin(&threads[i]);
    }

    TEST_CHECK(ctx, shared.value == 4000);

    Fluxion_Platform_MutexDestroy(&shared.mutex);
}
