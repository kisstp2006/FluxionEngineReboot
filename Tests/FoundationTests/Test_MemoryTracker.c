#include "TestFramework.h"

#include <Fluxion/Foundation/Memory/MemoryTracker.h>
#include <Fluxion/Foundation/Memory/TrackingAllocator.h>

void Test_MemoryTracker_Run(TestContext* ctx)
{
    const FluxionMemoryDomainId parentId = FLUXION_MEMORY_DOMAIN_ID_OF(TestParentDomain);
    const FluxionMemoryDomainId childId = FLUXION_MEMORY_DOMAIN_ID_OF(TestChildDomain);

    // Registration, duplicate rejection, unknown-parent rejection, and
    // rollup: an allocation recorded against the child must also show up
    // in the parent's totals.
    Fluxion_MemoryTracker_Init();
    {
        FluxionMemoryDomainDesc parentDesc = { parentId, "TestParentDomain", FLUXION_MEMORY_DOMAIN_ID_INVALID };
        FluxionMemoryDomainDesc childDesc = { childId, "TestChildDomain", parentId };

        TEST_CHECK(ctx, Fluxion_MemoryTracker_RegisterDomain(&parentDesc));
        TEST_CHECK(ctx, Fluxion_MemoryTracker_RegisterDomain(&parentDesc) == false); // duplicate
        TEST_CHECK(ctx, Fluxion_MemoryTracker_RegisterDomain(&childDesc));

        FluxionMemoryDomainDesc orphanDesc = { FLUXION_MEMORY_DOMAIN_ID_OF(TestOrphanDomain), "Orphan", FLUXION_MEMORY_DOMAIN_ID_OF(TestUnregisteredParent) };
        TEST_CHECK(ctx, Fluxion_MemoryTracker_RegisterDomain(&orphanDesc) == false); // parent not registered

        Fluxion_MemoryTracker_RecordAlloc(childId, 100);
        FluxionMemoryStatistics childStats = Fluxion_MemoryTracker_GetStatistics(childId);
        FluxionMemoryStatistics parentStats = Fluxion_MemoryTracker_GetStatistics(parentId);
        TEST_CHECK(ctx, childStats.allocationCount == 1 && childStats.currentBytes == 100);
        TEST_CHECK(ctx, parentStats.allocationCount == 1 && parentStats.currentBytes == 100);

        Fluxion_MemoryTracker_RecordFree(childId, 100);
        childStats = Fluxion_MemoryTracker_GetStatistics(childId);
        parentStats = Fluxion_MemoryTracker_GetStatistics(parentId);
        TEST_CHECK(ctx, childStats.deallocationCount == 1 && childStats.currentBytes == 0);
        TEST_CHECK(ctx, parentStats.deallocationCount == 1 && parentStats.currentBytes == 0);
        TEST_CHECK(ctx, childStats.peakBytes == 100 && parentStats.peakBytes == 100); // peak survives the free
    }
    Fluxion_MemoryTracker_Shutdown();

    // Push/pop scope stack.
    Fluxion_MemoryTracker_Init();
    {
        FluxionMemoryDomainDesc descA = { FLUXION_MEMORY_DOMAIN_ID_OF(TestStackDomainA), "A", FLUXION_MEMORY_DOMAIN_ID_INVALID };
        FluxionMemoryDomainDesc descB = { FLUXION_MEMORY_DOMAIN_ID_OF(TestStackDomainB), "B", FLUXION_MEMORY_DOMAIN_ID_INVALID };
        Fluxion_MemoryTracker_RegisterDomain(&descA);
        Fluxion_MemoryTracker_RegisterDomain(&descB);

        TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentDomain() == FLUXION_MEMORY_DOMAIN_ID_INVALID);

        Fluxion_MemoryTracker_PushDomain(descA.id);
        TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentDomain() == descA.id);

        Fluxion_MemoryTracker_PushDomain(descB.id);
        TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentDomain() == descB.id);

        Fluxion_MemoryTracker_PopDomain();
        TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentDomain() == descA.id);

        Fluxion_MemoryTracker_PopDomain();
        TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentDomain() == FLUXION_MEMORY_DOMAIN_ID_INVALID);
    }
    Fluxion_MemoryTracker_Shutdown();

    // Tracking allocator: alloc/free through it while a domain is pushed
    // must move that domain's statistics, and leave nothing dangling
    // once everything's freed.
    Fluxion_MemoryTracker_Init();
    {
        const FluxionMemoryDomainId allocDomain = FLUXION_MEMORY_DOMAIN_ID_OF(TestAllocatorDomain);
        FluxionMemoryDomainDesc desc = { allocDomain, "TestAllocatorDomain", FLUXION_MEMORY_DOMAIN_ID_INVALID };
        Fluxion_MemoryTracker_RegisterDomain(&desc);

        FluxionAllocator tracking;
        Fluxion_TrackingAllocator_Init(&tracking, Fluxion_DefaultAllocator());

        Fluxion_MemoryTracker_PushDomain(allocDomain);
        void* a = Fluxion_Allocator_Alloc(&tracking, 64, 8);
        void* b = Fluxion_Allocator_Alloc(&tracking, 64, 8);
        Fluxion_MemoryTracker_PopDomain();

        FluxionMemoryStatistics stats = Fluxion_MemoryTracker_GetStatistics(allocDomain);
        TEST_CHECK(ctx, stats.allocationCount == 2 && stats.currentBytes == 128 && stats.peakBytes == 128);

        Fluxion_MemoryTracker_PushDomain(allocDomain);
        Fluxion_Allocator_Free(&tracking, a, 64);
        Fluxion_Allocator_Free(&tracking, b, 64);
        Fluxion_MemoryTracker_PopDomain();

        stats = Fluxion_MemoryTracker_GetStatistics(allocDomain);
        TEST_CHECK(ctx, stats.deallocationCount == 2 && stats.currentBytes == 0 && stats.peakBytes == 128);
    }
    Fluxion_MemoryTracker_Shutdown();

    // Fixed-capacity registration limit.
    Fluxion_MemoryTracker_Init();
    {
        bool allRegistered = true;
        for (u32 i = 0; i < FLUXION_MAX_MEMORY_DOMAINS; ++i)
        {
            FluxionMemoryDomainDesc desc = { (FluxionMemoryDomainId)(i + 1000), "Filler", FLUXION_MEMORY_DOMAIN_ID_INVALID };
            if (!Fluxion_MemoryTracker_RegisterDomain(&desc))
            {
                allRegistered = false;
            }
        }
        TEST_CHECK(ctx, allRegistered);

        FluxionMemoryDomainDesc overflow = { (FluxionMemoryDomainId)99999, "Overflow", FLUXION_MEMORY_DOMAIN_ID_INVALID };
        TEST_CHECK(ctx, Fluxion_MemoryTracker_RegisterDomain(&overflow) == false);
    }
    Fluxion_MemoryTracker_Shutdown();
}
