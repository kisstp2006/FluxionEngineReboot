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

#include <Fluxion/Foundation/Atomic.h>
#include <Fluxion/Platform/Semaphore.h>
#include <Fluxion/Platform/Thread.h>
#include <Fluxion/Platform/Time.h>

typedef struct SemaphoreTestContext
{
    FluxionSemaphore semaphore;
    FluxionAtomicI32 wokenCount;
} SemaphoreTestContext;

static void SemaphoreWaiter(void* userData)
{
    SemaphoreTestContext* context = (SemaphoreTestContext*)userData;
    Fluxion_Platform_SemaphoreWait(&context->semaphore);
    Fluxion_AtomicI32_Increment(&context->wokenCount);
}

void Test_Semaphore_Run(TestContext* ctx)
{
    SemaphoreTestContext context;
    Fluxion_Platform_SemaphoreInit(&context.semaphore, 0, 4);
    Fluxion_AtomicI32_Store(&context.wokenCount, 0);

    FluxionThread threads[4];
    for (int i = 0; i < 4; ++i)
    {
        TEST_CHECK(ctx, Fluxion_Platform_ThreadCreate(&threads[i], SemaphoreWaiter, &context, NULL));
    }

    // Give the waiters time to actually reach Wait() -- not required for
    // correctness (a Signal before Wait just leaves the count elevated),
    // but makes the "still blocked" check below meaningful rather than
    // trivially true.
    Fluxion_Platform_SleepMilliseconds(50);
    TEST_CHECK(ctx, Fluxion_AtomicI32_Load(&context.wokenCount) == 0);

    Fluxion_Platform_SemaphoreSignal(&context.semaphore, 2);
    Fluxion_Platform_SleepMilliseconds(50);
    TEST_CHECK(ctx, Fluxion_AtomicI32_Load(&context.wokenCount) == 2);

    Fluxion_Platform_SemaphoreSignal(&context.semaphore, 2);

    for (int i = 0; i < 4; ++i)
    {
        Fluxion_Platform_ThreadJoin(&threads[i]);
    }

    TEST_CHECK(ctx, Fluxion_AtomicI32_Load(&context.wokenCount) == 4);

    Fluxion_Platform_SemaphoreDestroy(&context.semaphore);
}
