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

#include <Fluxion/Core/Service/ServiceRegistry.h>

typedef struct TestServiceV1
{
    FluxionServiceHeader header;
    i32 value;
} TestServiceV1;

static TestServiceV1 Test_Service_Make(FluxionServiceId id, u32 version, i32 value)
{
    TestServiceV1 service;
    service.header.serviceId = id;
    service.header.version = version;
    service.header.structSize = sizeof(TestServiceV1);
    service.value = value;
    return service;
}

void Test_Service_Run(TestContext* ctx)
{
    const FluxionServiceId idA = FLUXION_SERVICE_ID_OF(TestServiceA);
    const FluxionServiceId idB = FLUXION_SERVICE_ID_OF(TestServiceB);

    // Basic register/get/unregister.
    Fluxion_ServiceRegistry_Init();
    {
        TestServiceV1 serviceA = Test_Service_Make(idA, 1, 111);
        TEST_CHECK(ctx, Fluxion_ServiceRegistry_Register(&serviceA));

        const TestServiceV1* found = (const TestServiceV1*)Fluxion_ServiceRegistry_Get(idA, 1);
        TEST_CHECK(ctx, found == &serviceA);
        TEST_CHECK(ctx, found != NULL && found->value == 111);

        // Duplicate id rejected.
        TestServiceV1 duplicate = Test_Service_Make(idA, 1, 999);
        TEST_CHECK(ctx, Fluxion_ServiceRegistry_Register(&duplicate) == false);

        // Unrelated id never registered.
        TEST_CHECK(ctx, Fluxion_ServiceRegistry_Get(idB, 1) == NULL);

        Fluxion_ServiceRegistry_Unregister(idA);
        TEST_CHECK(ctx, Fluxion_ServiceRegistry_Get(idA, 1) == NULL);

        // Unregistering an id that isn't registered is a harmless no-op.
        Fluxion_ServiceRegistry_Unregister(idA);
    }
    Fluxion_ServiceRegistry_Shutdown();

    // Version gating: Get only succeeds for minVersion <= the registered version.
    Fluxion_ServiceRegistry_Init();
    {
        TestServiceV1 serviceB = Test_Service_Make(idB, 2, 222);
        TEST_CHECK(ctx, Fluxion_ServiceRegistry_Register(&serviceB));

        TEST_CHECK(ctx, Fluxion_ServiceRegistry_Get(idB, 1) != NULL);
        TEST_CHECK(ctx, Fluxion_ServiceRegistry_Get(idB, 2) != NULL);
        TEST_CHECK(ctx, Fluxion_ServiceRegistry_Get(idB, 3) == NULL);
    }
    Fluxion_ServiceRegistry_Shutdown();

    // Fixed-capacity registration limit.
    Fluxion_ServiceRegistry_Init();
    {
        TestServiceV1 filler[FLUXION_MAX_SERVICES];
        bool allRegistered = true;
        for (u32 i = 0; i < FLUXION_MAX_SERVICES; ++i)
        {
            filler[i] = Test_Service_Make((FluxionServiceId)(i + 1000), 1, (i32)i);
            if (!Fluxion_ServiceRegistry_Register(&filler[i]))
            {
                allRegistered = false;
            }
        }
        TEST_CHECK(ctx, allRegistered);

        TestServiceV1 overflow = Test_Service_Make((FluxionServiceId)99999, 1, 0);
        TEST_CHECK(ctx, Fluxion_ServiceRegistry_Register(&overflow) == false);
    }
    Fluxion_ServiceRegistry_Shutdown();
}
