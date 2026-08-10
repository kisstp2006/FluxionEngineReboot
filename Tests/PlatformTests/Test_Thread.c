#include "TestFramework.h"

#include <Fluxion/Foundation/Atomic.h>
#include <Fluxion/Platform/Thread.h>
#include <Fluxion/Platform/Time.h>

static void ThreadBody(void* userData)
{
    FluxionAtomicI32* counter = (FluxionAtomicI32*)userData;
    Fluxion_Platform_SetCurrentThreadName("FluxionTestThread");
    Fluxion_AtomicI32_Increment(counter);
}

void Test_Thread_Run(TestContext* ctx)
{
    FluxionAtomicI32 counter = { 0 };

    FluxionThread thread;
    TEST_CHECK(ctx, Fluxion_Platform_ThreadCreate(&thread, ThreadBody, &counter, "FluxionWorker"));
    Fluxion_Platform_ThreadJoin(&thread);

    TEST_CHECK(ctx, Fluxion_AtomicI32_Load(&counter) == 1);

    // Detach path: spin up another thread and just detach it, don't join.
    FluxionAtomicI32 counter2 = { 0 };
    FluxionThread thread2;
    TEST_CHECK(ctx, Fluxion_Platform_ThreadCreate(&thread2, ThreadBody, &counter2, NULL));
    Fluxion_Platform_ThreadDetach(&thread2);

    // Give the detached thread a moment to run before the test asserts.
    Fluxion_Platform_SleepMilliseconds(50);
    TEST_CHECK(ctx, Fluxion_AtomicI32_Load(&counter2) == 1);

    u64 currentId = Fluxion_Platform_GetCurrentThreadId();
    TEST_CHECK(ctx, currentId != 0);
}
