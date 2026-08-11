#include "TestFramework.h"

#include <Fluxion/Foundation/Atomic.h>
#include <Fluxion/Platform/Semaphore.h>
#include <Fluxion/Platform/Thread.h>
#include <Fluxion/Platform/Time.h>

typedef struct SemaphoreTestContext
{
    FluxionSemaphore semaphore;
    FluxionAtomicI32 wokenCount;
} SemaphoreTestContext;

static void SemaphoreWaiter(void* userData)
{
    SemaphoreTestContext* context = (SemaphoreTestContext*)userData;
    Fluxion_Platform_SemaphoreWait(&context->semaphore);
    Fluxion_AtomicI32_Increment(&context->wokenCount);
}

void Test_Semaphore_Run(TestContext* ctx)
{
    SemaphoreTestContext context;
    Fluxion_Platform_SemaphoreInit(&context.semaphore, 0, 4);
    Fluxion_AtomicI32_Store(&context.wokenCount, 0);

    FluxionThread threads[4];
    for (int i = 0; i < 4; ++i)
    {
        TEST_CHECK(ctx, Fluxion_Platform_ThreadCreate(&threads[i], SemaphoreWaiter, &context, NULL));
    }

    // Give the waiters time to actually reach Wait() -- not required for
    // correctness (a Signal before Wait just leaves the count elevated),
    // but makes the "still blocked" check below meaningful rather than
    // trivially true.
    Fluxion_Platform_SleepMilliseconds(50);
    TEST_CHECK(ctx, Fluxion_AtomicI32_Load(&context.wokenCount) == 0);

    Fluxion_Platform_SemaphoreSignal(&context.semaphore, 2);
    Fluxion_Platform_SleepMilliseconds(50);
    TEST_CHECK(ctx, Fluxion_AtomicI32_Load(&context.wokenCount) == 2);

    Fluxion_Platform_SemaphoreSignal(&context.semaphore, 2);

    for (int i = 0; i < 4; ++i)
    {
        Fluxion_Platform_ThreadJoin(&threads[i]);
    }

    TEST_CHECK(ctx, Fluxion_AtomicI32_Load(&context.wokenCount) == 4);

    Fluxion_Platform_SemaphoreDestroy(&context.semaphore);
}
