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
            TEST_CHECK(ctx, Fluxion_MemoryTracker_GetCurrentLocation().file != nullptr);

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
