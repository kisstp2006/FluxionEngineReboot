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
