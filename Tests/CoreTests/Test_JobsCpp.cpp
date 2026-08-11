#include "TestFramework.h"

#include <Fluxion/Core/Jobs/Jobs.hpp>
#include <Fluxion/Core/Jobs/JobSystem.h>
#include <Fluxion/Foundation/Atomic.h>

// main.c (plain C) calls this, so it needs unmangled C linkage.
extern "C" void Test_JobsCpp_Run(TestContext* ctx)
{
    Fluxion_JobSystem_Init(0, false);
    {
        FluxionAtomicI32 counter;
        Fluxion_AtomicI32_Store(&counter, 0);
        FluxionAtomicI32* counterPtr = &counter;

        FluxionJobHandle handle = Fluxion::Core::Jobs::Submit([counterPtr]()
        {
            Fluxion_AtomicI32_Increment(counterPtr);
        });
        Fluxion::Core::Jobs::Wait(handle);

        TEST_CHECK(ctx, Fluxion_AtomicI32_Load(&counter) == 1);
    }
    Fluxion_JobSystem_Shutdown();

    // ParallelFor via the C++ facade (single-pointer capture).
    Fluxion_JobSystem_Init(0, false);
    {
        enum { COUNT = 100 };
        FluxionAtomicI32 touched[COUNT];
        for (int i = 0; i < COUNT; ++i)
        {
            Fluxion_AtomicI32_Store(&touched[i], 0);
        }
        FluxionAtomicI32* touchedPtr = touched;

        FluxionJobHandle handle = Fluxion::Core::Jobs::ParallelFor(COUNT, 10, [touchedPtr](u32 index)
        {
            Fluxion_AtomicI32_Increment(&touchedPtr[index]);
        });
        Fluxion::Core::Jobs::Wait(handle);

        bool allTouchedOnce = true;
        for (int i = 0; i < COUNT; ++i)
        {
            if (Fluxion_AtomicI32_Load(&touched[i]) != 1)
            {
                allTouchedOnce = false;
                break;
            }
        }
        TEST_CHECK(ctx, allTouchedOnce);
    }
    Fluxion_JobSystem_Shutdown();
}
