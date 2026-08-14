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
