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

#include <Fluxion/Core/Startup/SubsystemRegistry.h>
#include <Fluxion/Foundation/Defines.h>

// Shared recording buffer: startup writes +tag, shutdown writes -tag-1 (so
// tag 0 still has a distinguishable shutdown encoding), in call order —
// tests assert on this sequence to prove dependency-driven ordering and
// exact-reverse shutdown, not just "it didn't crash".
static int s_order[FLUXION_MAX_SUBSYSTEMS];
static int s_orderCount;
static bool s_failC;

static void Test_Startup_ResetRecording(void)
{
    s_orderCount = 0;
    s_failC = false;
}

static void Test_Startup_Record(int tag)
{
    s_order[s_orderCount++] = tag;
}

static FluxionResult SubsystemA_Startup(void* userdata) { FLUXION_UNUSED(userdata); Test_Startup_Record(1); return Fluxion_ResultOk(); }
static void SubsystemA_Shutdown(void* userdata) { FLUXION_UNUSED(userdata); Test_Startup_Record(-2); }

static FluxionResult SubsystemB_Startup(void* userdata) { FLUXION_UNUSED(userdata); Test_Startup_Record(2); return Fluxion_ResultOk(); }
static void SubsystemB_Shutdown(void* userdata) { FLUXION_UNUSED(userdata); Test_Startup_Record(-3); }

static FluxionResult SubsystemC_Startup(void* userdata)
{
    FLUXION_UNUSED(userdata);
    if (s_failC)
    {
        return Fluxion_ResultError(1, "intentional test failure");
    }
    Test_Startup_Record(3);
    return Fluxion_ResultOk();
}
static void SubsystemC_Shutdown(void* userdata) { FLUXION_UNUSED(userdata); Test_Startup_Record(-4); }

static void Test_Startup_MakeDesc(
    FluxionSubsystemDesc* desc,
    FluxionSubsystemId id,
    const char* name,
    const FluxionSubsystemId* dependencies,
    u32 dependencyCount,
    FluxionResult (*startup)(void*),
    void (*shutdown)(void*))
{
    desc->id = id;
    desc->name = name;
    desc->phase = FLUXION_STARTUP_PHASE_CORE;
    desc->dependencies = dependencies;
    desc->dependencyCount = dependencyCount;
    desc->startup = startup;
    desc->shutdown = shutdown;
    desc->userdata = NULL;
}

void Test_Startup_Run(TestContext* ctx)
{
    const FluxionSubsystemId idA = FLUXION_SUBSYSTEM_ID_OF(TestSubsystemA);
    const FluxionSubsystemId idB = FLUXION_SUBSYSTEM_ID_OF(TestSubsystemB);
    const FluxionSubsystemId idC = FLUXION_SUBSYSTEM_ID_OF(TestSubsystemC);

    // Dependency-driven order + exact-reverse shutdown, registered in
    // scrambled (non-topological) order to prove order comes from the
    // dependency graph, not from registration order.
    Test_Startup_ResetRecording();
    Fluxion_SubsystemRegistry_Init();
    {
        FluxionSubsystemDesc descA, descB, descC;
        FluxionSubsystemId depsOfB[] = { idA };
        FluxionSubsystemId depsOfC[] = { idB };
        Test_Startup_MakeDesc(&descA, idA, "TestSubsystemA", NULL, 0, SubsystemA_Startup, SubsystemA_Shutdown);
        Test_Startup_MakeDesc(&descB, idB, "TestSubsystemB", depsOfB, 1, SubsystemB_Startup, SubsystemB_Shutdown);
        Test_Startup_MakeDesc(&descC, idC, "TestSubsystemC", depsOfC, 1, SubsystemC_Startup, SubsystemC_Shutdown);

        TEST_CHECK(ctx, Fluxion_SubsystemRegistry_Register(&descC));
        TEST_CHECK(ctx, Fluxion_SubsystemRegistry_Register(&descA));
        TEST_CHECK(ctx, Fluxion_SubsystemRegistry_Register(&descB));

        // Duplicate id must be rejected.
        TEST_CHECK(ctx, Fluxion_SubsystemRegistry_Register(&descA) == false);

        FluxionResult startResult = Fluxion_SubsystemRegistry_StartupAll();
        TEST_CHECK(ctx, startResult.ok);
        TEST_CHECK(ctx, Fluxion_SubsystemRegistry_IsRunning(idA));
        TEST_CHECK(ctx, Fluxion_SubsystemRegistry_IsRunning(idB));
        TEST_CHECK(ctx, Fluxion_SubsystemRegistry_IsRunning(idC));
        TEST_CHECK(ctx, s_orderCount == 3 && s_order[0] == 1 && s_order[1] == 2 && s_order[2] == 3);

        Fluxion_SubsystemRegistry_ShutdownAll();
        TEST_CHECK(ctx, !Fluxion_SubsystemRegistry_IsRunning(idA));
        TEST_CHECK(ctx, !Fluxion_SubsystemRegistry_IsRunning(idB));
        TEST_CHECK(ctx, !Fluxion_SubsystemRegistry_IsRunning(idC));
        TEST_CHECK(ctx, s_orderCount == 6 && s_order[3] == -4 && s_order[4] == -3 && s_order[5] == -2);
    }
    Fluxion_SubsystemRegistry_Shutdown();

    // Circular dependency: A -> B -> A. StartupAll must fail cleanly, and
    // nothing may end up running.
    Test_Startup_ResetRecording();
    Fluxion_SubsystemRegistry_Init();
    {
        FluxionSubsystemDesc descA, descB;
        FluxionSubsystemId depsOfA[] = { idB };
        FluxionSubsystemId depsOfB[] = { idA };
        Test_Startup_MakeDesc(&descA, idA, "TestSubsystemA", depsOfA, 1, SubsystemA_Startup, SubsystemA_Shutdown);
        Test_Startup_MakeDesc(&descB, idB, "TestSubsystemB", depsOfB, 1, SubsystemB_Startup, SubsystemB_Shutdown);

        TEST_CHECK(ctx, Fluxion_SubsystemRegistry_Register(&descA));
        TEST_CHECK(ctx, Fluxion_SubsystemRegistry_Register(&descB));

        FluxionResult startResult = Fluxion_SubsystemRegistry_StartupAll();
        TEST_CHECK(ctx, !startResult.ok);
        TEST_CHECK(ctx, s_orderCount == 0);
        TEST_CHECK(ctx, !Fluxion_SubsystemRegistry_IsRunning(idA));
        TEST_CHECK(ctx, !Fluxion_SubsystemRegistry_IsRunning(idB));
    }
    Fluxion_SubsystemRegistry_Shutdown();

    // Dependency on an id that was never registered must fail StartupAll.
    Test_Startup_ResetRecording();
    Fluxion_SubsystemRegistry_Init();
    {
        FluxionSubsystemDesc descA;
        FluxionSubsystemId depsOfA[] = { idC }; // TestSubsystemC is never registered here
        Test_Startup_MakeDesc(&descA, idA, "TestSubsystemA", depsOfA, 1, SubsystemA_Startup, SubsystemA_Shutdown);
        TEST_CHECK(ctx, Fluxion_SubsystemRegistry_Register(&descA));

        FluxionResult startResult = Fluxion_SubsystemRegistry_StartupAll();
        TEST_CHECK(ctx, !startResult.ok);
    }
    Fluxion_SubsystemRegistry_Shutdown();

    // A subsystem failing to start must roll back everything already
    // started during this StartupAll call, in reverse order.
    Test_Startup_ResetRecording();
    s_failC = true;
    Fluxion_SubsystemRegistry_Init();
    {
        FluxionSubsystemDesc descA, descB, descC;
        FluxionSubsystemId depsOfB[] = { idA };
        FluxionSubsystemId depsOfC[] = { idB };
        Test_Startup_MakeDesc(&descA, idA, "TestSubsystemA", NULL, 0, SubsystemA_Startup, SubsystemA_Shutdown);
        Test_Startup_MakeDesc(&descB, idB, "TestSubsystemB", depsOfB, 1, SubsystemB_Startup, SubsystemB_Shutdown);
        Test_Startup_MakeDesc(&descC, idC, "TestSubsystemC", depsOfC, 1, SubsystemC_Startup, SubsystemC_Shutdown);

        TEST_CHECK(ctx, Fluxion_SubsystemRegistry_Register(&descA));
        TEST_CHECK(ctx, Fluxion_SubsystemRegistry_Register(&descB));
        TEST_CHECK(ctx, Fluxion_SubsystemRegistry_Register(&descC));

        FluxionResult startResult = Fluxion_SubsystemRegistry_StartupAll();
        TEST_CHECK(ctx, !startResult.ok);
        TEST_CHECK(ctx, s_orderCount == 4 && s_order[0] == 1 && s_order[1] == 2 && s_order[2] == -3 && s_order[3] == -2);
        TEST_CHECK(ctx, !Fluxion_SubsystemRegistry_IsRunning(idA));
        TEST_CHECK(ctx, !Fluxion_SubsystemRegistry_IsRunning(idB));
        TEST_CHECK(ctx, !Fluxion_SubsystemRegistry_IsRunning(idC));
    }
    Fluxion_SubsystemRegistry_Shutdown();

    // Fixed-capacity registration limit.
    Fluxion_SubsystemRegistry_Init();
    {
        FluxionSubsystemDesc filler;
        Test_Startup_MakeDesc(&filler, 0, "Filler", NULL, 0, NULL, NULL);

        bool allRegistered = true;
        for (u32 i = 0; i < FLUXION_MAX_SUBSYSTEMS; ++i)
        {
            filler.id = (FluxionSubsystemId)(i + 1000);
            if (!Fluxion_SubsystemRegistry_Register(&filler))
            {
                allRegistered = false;
            }
        }
        TEST_CHECK(ctx, allRegistered);

        filler.id = (FluxionSubsystemId)99999;
        TEST_CHECK(ctx, Fluxion_SubsystemRegistry_Register(&filler) == false);
    }
    Fluxion_SubsystemRegistry_Shutdown();
}
