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

#include <Fluxion/RHI/RHI.h>

// Runs on the Null backend, whose fake GPU clock is monotonic and only
// advanced by WriteTimestamp -- which is exactly enough to prove the
// plumbing: what was written is what comes back, in order, and a reset
// actually clears.
void Test_Timestamps_Run(TestContext* ctx)
{
    FluxionRHIInstanceDesc instanceDesc = { "RHITests.Timestamps", false };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &instanceDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(instance));

    FluxionRHIAdapterHandle adapter;
    Fluxion_RHI_EnumerateAdapters(instance, &adapter, 1);
    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapter, &deviceDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(device));

    // The frequency is what turns ticks into time; a zero would make
    // every measurement a division by zero waiting to happen.
    TEST_CHECK(ctx, Fluxion_RHI_Device_GetTimestampFrequency(device) != 0);

    FluxionRHIQueryPoolHandle pool = Fluxion_RHI_CreateQueryPool(device, 8);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(pool));

    FluxionRHICommandListHandle cmd = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RHI_CommandList_Begin(cmd);
    Fluxion_RHI_CommandList_ResetQueryPool(cmd, pool, 0, 8);
    Fluxion_RHI_CommandList_WriteTimestamp(cmd, pool, 0);
    Fluxion_RHI_CommandList_WriteTimestamp(cmd, pool, 1);
    Fluxion_RHI_CommandList_End(cmd);

    u64 ticks[2] = { 0, 0 };
    TEST_CHECK(ctx, Fluxion_RHI_QueryPool_GetResults(pool, 0, 2, ticks));

    // Written later must read later -- a plumbing mistake that swapped
    // indices or read the wrong slot shows up right here.
    TEST_CHECK(ctx, ticks[0] != 0);
    TEST_CHECK(ctx, ticks[1] > ticks[0]);

    // A query that was never written reads as whatever reset left there,
    // not as a leftover from a neighbouring slot.
    u64 unwritten = 123;
    TEST_CHECK(ctx, Fluxion_RHI_QueryPool_GetResults(pool, 5, 1, &unwritten));
    TEST_CHECK(ctx, unwritten == 0);

    // Out-of-range reads are refused, not clamped -- clamping would hand
    // back fewer values than the caller asked for with no way to know.
    u64 dummy[16];
    TEST_CHECK(ctx, !Fluxion_RHI_QueryPool_GetResults(pool, 4, 8, dummy));

    Fluxion_RHI_DestroyCommandList(cmd);
    Fluxion_RHI_DestroyQueryPool(pool);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}
