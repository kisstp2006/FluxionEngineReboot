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

#include <Fluxion/Core/Jobs/JobSystem.h>
#include <Fluxion/Foundation/Atomic.h>
#include <Fluxion/Foundation/Handle.h>

#include <string.h>

typedef struct CounterJobData
{
    FluxionAtomicI32* counter;
} CounterJobData;

static void IncrementJob(void* data)
{
    CounterJobData* jobData = (CounterJobData*)data;
    Fluxion_AtomicI32_Increment(jobData->counter);
}

typedef struct OrderJobData
{
    i32* order;
    FluxionAtomicI32* nextSlot;
    i32 tag;
} OrderJobData;

static void OrderJob(void* data)
{
    OrderJobData* jobData = (OrderJobData*)data;
    i32 slot = Fluxion_AtomicI32_Increment(jobData->nextSlot) - 1;
    jobData->order[slot] = jobData->tag;
}

static void TouchIndex(void* userData, u32 index)
{
    FluxionAtomicI32* touched = (FluxionAtomicI32*)userData;
    Fluxion_AtomicI32_Increment(&touched[index]);
}

void Test_Jobs_Run(TestContext* ctx)
{
    // Basic submit + wait.
    Fluxion_JobSystem_Init(0, false);
    {
        FluxionAtomicI32 counter;
        Fluxion_AtomicI32_Store(&counter, 0);
        CounterJobData jobData;
        jobData.counter = &counter;

        FluxionJobDesc desc;
        desc.function = IncrementJob;
        memcpy(desc.data, &jobData, sizeof(jobData));
        desc.dataSize = sizeof(jobData);
        desc.dependencies = NULL;
        desc.dependencyCount = 0;

        FluxionJobHandle handle = Fluxion_JobSystem_Submit(&desc);
        Fluxion_JobSystem_Wait(handle);

        TEST_CHECK(ctx, Fluxion_AtomicI32_Load(&counter) == 1);
    }
    Fluxion_JobSystem_Shutdown();

    // Dependency chain A -> B -> C must run in that order.
    Fluxion_JobSystem_Init(0, false);
    {
        i32 order[3] = { -1, -1, -1 };
        FluxionAtomicI32 nextSlot;
        Fluxion_AtomicI32_Store(&nextSlot, 0);

        OrderJobData dataA = { order, &nextSlot, 1 };
        OrderJobData dataB = { order, &nextSlot, 2 };
        OrderJobData dataC = { order, &nextSlot, 3 };

        FluxionJobDesc descA;
        descA.function = OrderJob;
        memcpy(descA.data, &dataA, sizeof(dataA));
        descA.dataSize = sizeof(dataA);
        descA.dependencies = NULL;
        descA.dependencyCount = 0;
        FluxionJobHandle handleA = Fluxion_JobSystem_Submit(&descA);

        FluxionJobDesc descB;
        descB.function = OrderJob;
        memcpy(descB.data, &dataB, sizeof(dataB));
        descB.dataSize = sizeof(dataB);
        descB.dependencies = &handleA;
        descB.dependencyCount = 1;
        FluxionJobHandle handleB = Fluxion_JobSystem_Submit(&descB);

        FluxionJobDesc descC;
        descC.function = OrderJob;
        memcpy(descC.data, &dataC, sizeof(dataC));
        descC.dataSize = sizeof(dataC);
        descC.dependencies = &handleB;
        descC.dependencyCount = 1;
        FluxionJobHandle handleC = Fluxion_JobSystem_Submit(&descC);

        Fluxion_JobSystem_Wait(handleC);

        TEST_CHECK(ctx, order[0] == 1 && order[1] == 2 && order[2] == 3);
    }
    Fluxion_JobSystem_Shutdown();

    // ParallelFor touches every index exactly once.
    Fluxion_JobSystem_Init(0, false);
    {
        enum { COUNT = 200 };
        FluxionAtomicI32 touched[COUNT];
        for (int i = 0; i < COUNT; ++i)
        {
            Fluxion_AtomicI32_Store(&touched[i], 0);
        }

        FluxionJobHandle handle = Fluxion_JobSystem_ParallelFor(COUNT, 16, TouchIndex, touched, NULL, 0);
        Fluxion_JobSystem_Wait(handle);

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

    // CombineDependencies: a job depending on the combined handle must
    // only run after both source jobs finish.
    Fluxion_JobSystem_Init(0, false);
    {
        FluxionAtomicI32 counter;
        Fluxion_AtomicI32_Store(&counter, 0);
        CounterJobData jobData;
        jobData.counter = &counter;

        FluxionJobDesc descA;
        descA.function = IncrementJob;
        memcpy(descA.data, &jobData, sizeof(jobData));
        descA.dataSize = sizeof(jobData);
        descA.dependencies = NULL;
        descA.dependencyCount = 0;
        FluxionJobHandle handleA = Fluxion_JobSystem_Submit(&descA);

        FluxionJobDesc descB;
        descB.function = IncrementJob;
        memcpy(descB.data, &jobData, sizeof(jobData));
        descB.dataSize = sizeof(jobData);
        descB.dependencies = NULL;
        descB.dependencyCount = 0;
        FluxionJobHandle handleB = Fluxion_JobSystem_Submit(&descB);

        FluxionJobHandle combined[2] = { handleA, handleB };
        FluxionJobHandle combinedHandle = Fluxion_JobSystem_CombineDependencies(combined, 2);

        i32 order[1] = { -1 };
        FluxionAtomicI32 nextSlot;
        Fluxion_AtomicI32_Store(&nextSlot, 0);
        OrderJobData finalData = { order, &nextSlot, 99 };

        FluxionJobDesc descFinal;
        descFinal.function = OrderJob;
        memcpy(descFinal.data, &finalData, sizeof(finalData));
        descFinal.dataSize = sizeof(finalData);
        descFinal.dependencies = &combinedHandle;
        descFinal.dependencyCount = 1;
        FluxionJobHandle handleFinal = Fluxion_JobSystem_Submit(&descFinal);

        Fluxion_JobSystem_Wait(handleFinal);

        TEST_CHECK(ctx, Fluxion_AtomicI32_Load(&counter) == 2);
        TEST_CHECK(ctx, order[0] == 99);
    }
    Fluxion_JobSystem_Shutdown();

    // Single-threaded mode: deterministic, synchronous, submission order --
    // Submit() itself has already run the job by the time it returns.
    Fluxion_JobSystem_Init(0, true);
    {
        i32 order[3] = { -1, -1, -1 };
        FluxionAtomicI32 nextSlot;
        Fluxion_AtomicI32_Store(&nextSlot, 0);

        for (i32 tag = 1; tag <= 3; ++tag)
        {
            OrderJobData data;
            data.order = order;
            data.nextSlot = &nextSlot;
            data.tag = tag;

            FluxionJobDesc desc;
            desc.function = OrderJob;
            memcpy(desc.data, &data, sizeof(data));
            desc.dataSize = sizeof(data);
            desc.dependencies = NULL;
            desc.dependencyCount = 0;

            FluxionJobHandle handle = Fluxion_JobSystem_Submit(&desc);
            TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(handle));
        }

        TEST_CHECK(ctx, order[0] == 1 && order[1] == 2 && order[2] == 3);
    }
    Fluxion_JobSystem_Shutdown();
}
