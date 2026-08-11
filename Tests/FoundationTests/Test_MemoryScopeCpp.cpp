#include "TestFramework.h"

#include <Fluxion/Foundation/Memory/MemoryScope.hpp>
#include <Fluxion/Foundation/Memory/MemoryTracker.h>

// main.c (plain C) calls this, so it needs unmangled C linkage.
extern "C" void Test_MemoryScopeCpp_Run(TestContext* ctx)
{
    const FluxionMemoryDomainId domainA = FLUXION_MEMORY_DOMAIN_ID_OF(TestCppScopeDomainA);
    const FluxionMemoryDomainId domainB = FLUXION_MEMORY_DOMAIN_ID_OF(TestCppScopeDomainB);

    Fluxion_MemoryTracker_Init();
    {
        FluxionMemoryDomainDesc descA = { domainA, "A", FLUXION_MEMORY_DOMAIN_ID_INVALID };
        FluxionMemoryDomainDesc descB = { domainB, "B", FLUXION_MEMORY_DOMAIN_ID_INVALID };
        Fluxion_MemoryTracker_RegisterDomain(&descA);
        Fluxion_MemoryTracker_RegisterDomain(&descB);

        TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentDomain() == FLUXION_MEMORY_DOMAIN_ID_INVALID);

        {
            FLUXION_MEMORY_SCOPE(domainA);
            TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentDomain() == domainA);

            {
                FLUXION_MEMORY_SCOPE(domainB);
                TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentDomain() == domainB);
            }

            TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentDomain() == domainA);
        }

        TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentDomain() == FLUXION_MEMORY_DOMAIN_ID_INVALID);
    }
    Fluxion_MemoryTracker_Shutdown();
}
