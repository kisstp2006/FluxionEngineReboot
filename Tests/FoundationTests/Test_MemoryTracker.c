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

#include "TestFramework.h"

#include <string.h>

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
        TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentLocation().file == NULL);

        const FluxionSourceLocation locationA = { __FILE__, __func__, (u32)__LINE__ };
        Fluxion_MemoryTracker_PushDomain(descA.id, locationA);
        TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentDomain() == descA.id);
        TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentLocation().line == locationA.line);

        const FluxionSourceLocation locationB = { __FILE__, __func__, (u32)__LINE__ };
        Fluxion_MemoryTracker_PushDomain(descB.id, locationB);
        TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentDomain() == descB.id);
        TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentLocation().line == locationB.line);

        Fluxion_MemoryTracker_PopDomain();
        TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentDomain() == descA.id);
        TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentLocation().line == locationA.line);

        Fluxion_MemoryTracker_PopDomain();
        TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentDomain() == FLUXION_MEMORY_DOMAIN_ID_INVALID);
        TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentLocation().file == NULL);

        TEST_CHECK(ctx, strcmp(Fluxion_MemoryTracker_GetDomainName(descA.id), "A") == 0);
        TEST_CHECK(ctx, Fluxion_MemoryTracker_GetDomainName(FLUXION_MEMORY_DOMAIN_ID_OF(TestUnregisteredDomain)) == NULL);
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

        Fluxion_MemoryTracker_PushDomain(allocDomain, (FluxionSourceLocation){ __FILE__, __func__, (u32)__LINE__ });
        void* a = Fluxion_Allocator_Alloc(&tracking, 64, 8);
        void* b = Fluxion_Allocator_Alloc(&tracking, 64, 8);
        Fluxion_MemoryTracker_PopDomain();

        FluxionMemoryStatistics stats = Fluxion_MemoryTracker_GetStatistics(allocDomain);
        TEST_CHECK(ctx, stats.allocationCount == 2 && stats.currentBytes == 128 && stats.peakBytes == 128);

        Fluxion_MemoryTracker_PushDomain(allocDomain, (FluxionSourceLocation){ __FILE__, __func__, (u32)__LINE__ });
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
