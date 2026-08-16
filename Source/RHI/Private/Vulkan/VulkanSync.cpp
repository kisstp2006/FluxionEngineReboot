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

// Synchronization objects. Every FluxionRHIFenceHandle is backed by a
// dedicated *timeline* VkSemaphore (Vulkan 1.2 core) rather than a
// classic VkFence -- WaitForFence/ResetFence are implemented in terms of
// a per-fence target value so the public contract keeps classic fence
// semantics (signaled/reset/wait) while the implementation gets a single
// primitive that also collapses fence and semaphore into one object and
// supports waiting for an arbitrary future value rather than only a
// fixed 1:1 signal. FluxionRHISemaphoreHandle stays a classic *binary*
// VkSemaphore -- the only two contract call sites that take one
// (Swapchain_AcquireNextImage/Present) are exactly the acquire/present
// boundary where Vulkan still requires a binary semaphore.

#include "VulkanCommon.h"

#include <Fluxion/Foundation/Log.h>

struct FluxionRHIVulkanFence
{
    VkSemaphore semaphore = VK_NULL_HANDLE;
    u64 nextSignalValue = 1;
    u64 waitTarget = 0;
};

static FluxionRHIVulkanSlot s_fenceSlots[FLUXION_RHI_VULKAN_MAX_FENCES];
static FluxionRHIVulkanFence s_fences[FLUXION_RHI_VULKAN_MAX_FENCES];

static FluxionRHIVulkanSlot s_semaphoreSlots[FLUXION_RHI_VULKAN_MAX_SEMAPHORES];
static VkSemaphore s_semaphores[FLUXION_RHI_VULKAN_MAX_SEMAPHORES];

static FluxionRHIVulkanSlot s_queryPoolSlots[FLUXION_RHI_VULKAN_MAX_QUERY_POOLS];
static VkQueryPool s_queryPools[FLUXION_RHI_VULKAN_MAX_QUERY_POOLS];

// --- Fences ------------------------------------------------------------

FluxionRHIFenceHandle Fluxion_RHIVulkan_CreateFence(FluxionRHIDeviceHandle device, bool signaled)
{
    FluxionRHIFenceHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    if (deviceState == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIVulkan_PoolAllocate(s_fenceSlots, FLUXION_RHI_VULKAN_MAX_FENCES, &index, &generation)) return invalid;

    VkSemaphoreTypeCreateInfo timelineType = {};
    timelineType.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineType.initialValue = 0;
    VkSemaphoreCreateInfo semInfo = {};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semInfo.pNext = &timelineType;

    FluxionRHIVulkanFence* fence = &s_fences[index];
    *fence = FluxionRHIVulkanFence{};
    if (vkCreateSemaphore(deviceState->device, &semInfo, nullptr, &fence->semaphore) != VK_SUCCESS)
    {
        Fluxion_RHIVulkan_PoolFree(s_fenceSlots, FLUXION_RHI_VULKAN_MAX_FENCES, index, generation);
        return invalid;
    }
    fence->nextSignalValue = 1;
    fence->waitTarget = signaled ? 0 : 1; // value 0 is the semaphore's initial value -- waiting for it is an immediate no-op

    FluxionRHIFenceHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIVulkan_DestroyFence(FluxionRHIFenceHandle fence)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_fenceSlots, FLUXION_RHI_VULKAN_MAX_FENCES, fence.index, fence.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: DestroyFence called with an invalid or already-destroyed handle");
        return;
    }
    FluxionRHIVulkanFence* fenceState = &s_fences[fence.index];
    if (fenceState->semaphore != VK_NULL_HANDLE) vkDestroySemaphore(Fluxion_RHIVulkan_GetOwningDevice(), fenceState->semaphore, nullptr);
    *fenceState = FluxionRHIVulkanFence{};
    Fluxion_RHIVulkan_PoolFree(s_fenceSlots, FLUXION_RHI_VULKAN_MAX_FENCES, fence.index, fence.generation);
}

bool Fluxion_RHIVulkan_WaitForFence(FluxionRHIFenceHandle fence)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_fenceSlots, FLUXION_RHI_VULKAN_MAX_FENCES, fence.index, fence.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: WaitForFence called with an invalid fence handle");
        return false;
    }
    FluxionRHIVulkanFence* fenceState = &s_fences[fence.index];
    if (fenceState->waitTarget == 0) return true; // created/reset-to-signaled, nothing to wait for

    VkSemaphoreWaitInfo waitInfo = {};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &fenceState->semaphore;
    waitInfo.pValues = &fenceState->waitTarget;

    // Bounded, not VK_TIMEOUT-never (UINT64_MAX) -- observed in practice
    // (RTX 2060, driver 581.29, Vulkan SDK 1.4.350 validation layer) that
    // VK_LAYER_KHRONOS_validation's own background submission-tracking
    // thread (vvl::Queue::ThreadFunc) can wedge after a few hundred
    // frames of steady per-frame timeline-semaphore submission, which
    // then blocks this CPU-side wait forever even though the GPU itself
    // never hangs (no TDR event is ever recorded) -- confirmed by
    // reproducing the hang, then reproducing a clean unbounded run with
    // the layer disabled (VK_LOADER_LAYERS_DISABLE=VK_LAYER_KHRONOS_validation).
    // An indefinite wait here stalls the caller's whole message pump,
    // which Windows then reports as "Not Responding". A several-second
    // bound keeps any single stall well under that watchdog's threshold
    // and turns a silent external-layer wedge into a visible, recoverable
    // log line instead of a dead window.
    VkResult waitResult = vkWaitSemaphores(Fluxion_RHIVulkan_GetOwningDevice(), &waitInfo, 4000000000ull);
    if (waitResult != VK_SUCCESS)
    {
        u64 currentValue = 0;
        vkGetSemaphoreCounterValue(Fluxion_RHIVulkan_GetOwningDevice(), fenceState->semaphore, &currentValue);
        FLUXION_LOG_ERROR("RHIVulkan", "WaitForFence timed out (VkResult=%d): waitTarget=%llu, currentSemaphoreValue=%llu -- GPU submission did not complete in time",
            (int)waitResult, (unsigned long long)fenceState->waitTarget, (unsigned long long)currentValue);
        return false;
    }
    return true;
}

void Fluxion_RHIVulkan_ResetFence(FluxionRHIFenceHandle fence)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_fenceSlots, FLUXION_RHI_VULKAN_MAX_FENCES, fence.index, fence.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: ResetFence called with an invalid fence handle");
        return;
    }
    // A timeline semaphore's value never decreases -- "reset" just means
    // the fence should now wait for the *next* submission that signals
    // it, i.e. the value that submission will produce.
    s_fences[fence.index].waitTarget = s_fences[fence.index].nextSignalValue;
}

VkSemaphore Fluxion_RHIVulkan_FenceBeginSignal(FluxionRHIFenceHandle fence, u64* outValue)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_fenceSlots, FLUXION_RHI_VULKAN_MAX_FENCES, fence.index, fence.generation)) return VK_NULL_HANDLE;
    FluxionRHIVulkanFence* fenceState = &s_fences[fence.index];
    *outValue = fenceState->nextSignalValue;
    fenceState->waitTarget = fenceState->nextSignalValue;
    ++fenceState->nextSignalValue;
    return fenceState->semaphore;
}

// --- Binary semaphores (acquire/present boundary only) ----------------------

FluxionRHISemaphoreHandle Fluxion_RHIVulkan_CreateSemaphore(FluxionRHIDeviceHandle device)
{
    FluxionRHISemaphoreHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    if (deviceState == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIVulkan_PoolAllocate(s_semaphoreSlots, FLUXION_RHI_VULKAN_MAX_SEMAPHORES, &index, &generation)) return invalid;

    VkSemaphoreCreateInfo semInfo = {};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(deviceState->device, &semInfo, nullptr, &s_semaphores[index]) != VK_SUCCESS)
    {
        Fluxion_RHIVulkan_PoolFree(s_semaphoreSlots, FLUXION_RHI_VULKAN_MAX_SEMAPHORES, index, generation);
        return invalid;
    }

    FluxionRHISemaphoreHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIVulkan_DestroySemaphore(FluxionRHISemaphoreHandle semaphore)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_semaphoreSlots, FLUXION_RHI_VULKAN_MAX_SEMAPHORES, semaphore.index, semaphore.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: DestroySemaphore called with an invalid or already-destroyed handle");
        return;
    }
    if (s_semaphores[semaphore.index] != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(Fluxion_RHIVulkan_GetOwningDevice(), s_semaphores[semaphore.index], nullptr);
        s_semaphores[semaphore.index] = VK_NULL_HANDLE;
    }
    Fluxion_RHIVulkan_PoolFree(s_semaphoreSlots, FLUXION_RHI_VULKAN_MAX_SEMAPHORES, semaphore.index, semaphore.generation);
}

VkSemaphore Fluxion_RHIVulkan_ResolveBinarySemaphore(FluxionRHISemaphoreHandle semaphore)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_semaphoreSlots, FLUXION_RHI_VULKAN_MAX_SEMAPHORES, semaphore.index, semaphore.generation)) return VK_NULL_HANDLE;
    return s_semaphores[semaphore.index];
}

// --- Query pools --------------------------------------------------------

// Remembered at create time so GetResults can bound its reads -- Vulkan
// itself has no "how big is this pool" query.
static u32 s_queryPoolCounts[FLUXION_RHI_VULKAN_MAX_QUERY_POOLS];

FluxionRHIQueryPoolHandle Fluxion_RHIVulkan_CreateQueryPool(FluxionRHIDeviceHandle device, u32 queryCount)
{
    FluxionRHIQueryPoolHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    if (deviceState == nullptr || queryCount == 0) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIVulkan_PoolAllocate(s_queryPoolSlots, FLUXION_RHI_VULKAN_MAX_QUERY_POOLS, &index, &generation)) return invalid;

    VkQueryPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    poolInfo.queryCount = queryCount;
    if (vkCreateQueryPool(deviceState->device, &poolInfo, nullptr, &s_queryPools[index]) != VK_SUCCESS)
    {
        Fluxion_RHIVulkan_PoolFree(s_queryPoolSlots, FLUXION_RHI_VULKAN_MAX_QUERY_POOLS, index, generation);
        return invalid;
    }
    s_queryPoolCounts[index] = queryCount;

    FluxionRHIQueryPoolHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIVulkan_DestroyQueryPool(FluxionRHIQueryPoolHandle queryPool)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_queryPoolSlots, FLUXION_RHI_VULKAN_MAX_QUERY_POOLS, queryPool.index, queryPool.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: DestroyQueryPool called with an invalid or already-destroyed handle");
        return;
    }
    if (s_queryPools[queryPool.index] != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(Fluxion_RHIVulkan_GetOwningDevice(), s_queryPools[queryPool.index], nullptr);
        s_queryPools[queryPool.index] = VK_NULL_HANDLE;
    }
    Fluxion_RHIVulkan_PoolFree(s_queryPoolSlots, FLUXION_RHI_VULKAN_MAX_QUERY_POOLS, queryPool.index, queryPool.generation);
}

void Fluxion_RHIVulkan_CommandListResetQueryPool(FluxionRHICommandListHandle commandList, FluxionRHIQueryPoolHandle queryPool, u32 firstQuery, u32 queryCount)
{
    VkCommandBuffer cmd = Fluxion_RHIVulkan_ResolveCommandBuffer(commandList);
    if (cmd == VK_NULL_HANDLE) return;
    if (!Fluxion_RHIVulkan_PoolIsValid(s_queryPoolSlots, FLUXION_RHI_VULKAN_MAX_QUERY_POOLS, queryPool.index, queryPool.generation)) return;
    vkCmdResetQueryPool(cmd, s_queryPools[queryPool.index], firstQuery, queryCount);
}

void Fluxion_RHIVulkan_CommandListWriteTimestamp(FluxionRHICommandListHandle commandList, FluxionRHIQueryPoolHandle queryPool, u32 queryIndex)
{
    VkCommandBuffer cmd = Fluxion_RHIVulkan_ResolveCommandBuffer(commandList);
    if (cmd == VK_NULL_HANDLE) return;
    if (!Fluxion_RHIVulkan_PoolIsValid(s_queryPoolSlots, FLUXION_RHI_VULKAN_MAX_QUERY_POOLS, queryPool.index, queryPool.generation)) return;
    // BOTTOM_OF_PIPE: the timestamp lands once everything recorded before
    // it has actually finished, which is what "how long did that take"
    // needs -- TOP_OF_PIPE would measure when the GPU merely started
    // looking at the commands.
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, s_queryPools[queryPool.index], queryIndex);
}

bool Fluxion_RHIVulkan_QueryPoolGetResults(FluxionRHIQueryPoolHandle queryPool, u32 firstQuery, u32 queryCount, u64* outTicks)
{
    if (outTicks == nullptr || queryCount == 0) return false;
    if (!Fluxion_RHIVulkan_PoolIsValid(s_queryPoolSlots, FLUXION_RHI_VULKAN_MAX_QUERY_POOLS, queryPool.index, queryPool.generation)) return false;
    if (firstQuery + queryCount > s_queryPoolCounts[queryPool.index]) return false;

    // WITH_AVAILABILITY interleaves an availability word after each
    // value, which is what lets this return false for a result the GPU
    // has not produced yet instead of handing back stale bytes -- the
    // one caller mistake this backend can actually catch.
    u64 paired[128 * 2];
    if (queryCount > 128) return false;
    VkResult result = vkGetQueryPoolResults(Fluxion_RHIVulkan_GetOwningDevice(), s_queryPools[queryPool.index],
        firstQuery, queryCount, sizeof(u64) * 2 * queryCount, paired, sizeof(u64) * 2,
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (result != VK_SUCCESS) return false;
    for (u32 i = 0; i < queryCount; ++i)
    {
        if (paired[i * 2 + 1] == 0) return false; // not yet available
        outTicks[i] = paired[i * 2];
    }
    return true;
}

u64 Fluxion_RHIVulkan_GetTimestampFrequency(FluxionRHIDeviceHandle device)
{
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    if (deviceState == nullptr) return 0;
    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties(deviceState->physicalDevice, &properties);
    // timestampPeriod is nanoseconds per tick; the contract wants ticks
    // per second. A period of exactly 1.0 (common) lands on 1e9.
    if (properties.limits.timestampPeriod <= 0.0f) return 0;
    return (u64)(1000000000.0 / (double)properties.limits.timestampPeriod);
}
