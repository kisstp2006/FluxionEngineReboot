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

// Surface + swapchain. The surface is created from the Application
// module's native window/display escape hatches (Win32: HWND + HINSTANCE
// from GetModuleHandle; Linux: Xlib Window + Display* via
// Fluxion_WindowSystem_GetNativeDisplayHandle). Recreation on resize:
// recreate on VK_ERROR_OUT_OF_DATE_KHR (skip the frame, no submit) and on
// VK_SUBOPTIMAL_KHR (present this frame, then recreate), with an
// explicit 0x0 (minimized) guard.
//
// Acquire correctness: the RHI contract's Queue_Submit has no
// wait-semaphore parameter, so there is no portable way for a submitted
// command buffer to wait on the acquire semaphore GPU-side.
// AcquireNextImage here therefore also waits on an internal VkFence
// CPU-side before returning, guaranteeing the image is truly available
// by the time the caller records/submits against it -- this trades away
// some CPU/GPU overlap for correctness within the current contract;
// revisit if/when Queue_Submit grows a wait-semaphore list.

#include "VulkanCommon.h"

#include <Fluxion/Application/Window/Window.h>

struct FluxionRHIVulkanSwapchain
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFence acquireFence = VK_NULL_HANDLE;
    FluxionRHITextureHandle images[FLUXION_RHI_VULKAN_MAX_SWAPCHAIN_IMAGES];
    u32 imageCount = 0;
    u32 width = 0;
    u32 height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    u32 requestedBufferCount = 2;
    bool vsync = true;
    FluxionWindowHandle window{};
};

static FluxionRHIVulkanSlot s_swapchainSlots[FLUXION_RHI_VULKAN_MAX_SWAPCHAINS];
static FluxionRHIVulkanSwapchain s_swapchains[FLUXION_RHI_VULKAN_MAX_SWAPCHAINS];

static VkSurfaceKHR Fluxion_RHIVulkan_CreateSurface(FluxionWindowHandle window)
{
    FluxionNativeWindowHandle native = Fluxion_Window_GetNativeHandle(window);
    if (native.value == nullptr) return VK_NULL_HANDLE;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
#if defined(_WIN32)
    VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hinstance = GetModuleHandle(nullptr);
    surfaceInfo.hwnd = (HWND)native.value;
    vkCreateWin32SurfaceKHR(Fluxion_RHIVulkan_GetInstance(), &surfaceInfo, nullptr, &surface);
#else
    VkXlibSurfaceCreateInfoKHR surfaceInfo = {};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.dpy = (Display*)Fluxion_WindowSystem_GetNativeDisplayHandle();
    surfaceInfo.window = (Window)(uintptr_t)native.value;
    vkCreateXlibSurfaceKHR(Fluxion_RHIVulkan_GetInstance(), &surfaceInfo, nullptr, &surface);
#endif
    return surface;
}

static void Fluxion_RHIVulkan_DestroySwapchainImages(FluxionRHIVulkanSwapchain* sc)
{
    for (u32 i = 0; i < sc->imageCount; ++i)
    {
        Fluxion_RHIVulkan_FreeTextureSlotDirect(sc->images[i]);
    }
    sc->imageCount = 0;
}

// Builds (or rebuilds, on resize/OUT_OF_DATE) the actual VkSwapchainKHR
// against the already-created surface, tearing down only the swapchain +
// its image slots (the surface and acquire fence are reused).
static bool Fluxion_RHIVulkan_BuildSwapchain(FluxionRHIVulkanDevice* deviceState, FluxionRHIVulkanSwapchain* sc)
{
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(deviceState->physicalDevice, sc->surface, &caps);

    u32 width = caps.currentExtent.width;
    u32 height = caps.currentExtent.height;
    if (width == 0xFFFFFFFFu)
    {
        u32 windowWidth = 0, windowHeight = 0;
        Fluxion_Window_GetSize(sc->window, &windowWidth, &windowHeight);
        width = windowWidth;
        height = windowHeight;
    }
    if (width == 0 || height == 0) return false; // minimized -- try again next frame

    VkSwapchainKHR oldSwapchain = sc->swapchain;

    u32 imageCount = sc->requestedBufferCount;
    if (imageCount < caps.minImageCount) imageCount = caps.minImageCount;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;
    if (imageCount > FLUXION_RHI_VULKAN_MAX_SWAPCHAIN_IMAGES) imageCount = FLUXION_RHI_VULKAN_MAX_SWAPCHAIN_IMAGES;

    VkSwapchainCreateInfoKHR swapchainInfo = {};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = sc->surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = sc->format;
    swapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchainInfo.imageExtent = { width, height };
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = caps.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = sc->presentMode;
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = oldSwapchain;

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    VkResult result = vkCreateSwapchainKHR(deviceState->device, &swapchainInfo, nullptr, &newSwapchain);

    if (oldSwapchain != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(deviceState->device); // simplest-correct v1: no overlap between old/new swapchain lifetime
        Fluxion_RHIVulkan_DestroySwapchainImages(sc);
        vkDestroySwapchainKHR(deviceState->device, oldSwapchain, nullptr);
    }

    if (result != VK_SUCCESS)
    {
        sc->swapchain = VK_NULL_HANDLE;
        return false;
    }
    sc->swapchain = newSwapchain;
    sc->width = width;
    sc->height = height;

    u32 actualImageCount = 0;
    vkGetSwapchainImagesKHR(deviceState->device, sc->swapchain, &actualImageCount, nullptr);
    VkImage vkImages[FLUXION_RHI_VULKAN_MAX_SWAPCHAIN_IMAGES];
    actualImageCount = actualImageCount > FLUXION_RHI_VULKAN_MAX_SWAPCHAIN_IMAGES ? FLUXION_RHI_VULKAN_MAX_SWAPCHAIN_IMAGES : actualImageCount;
    vkGetSwapchainImagesKHR(deviceState->device, sc->swapchain, &actualImageCount, vkImages);

    sc->imageCount = 0;
    for (u32 i = 0; i < actualImageCount; ++i)
    {
        FluxionRHITextureHandle handle;
        u32 index = 0;
        if (!Fluxion_RHIVulkan_AllocateTextureSlot(&handle, &index)) break;
        FluxionRHIVulkanTexture* texture = Fluxion_RHIVulkan_ResolveTexture(handle);
        texture->image = vkImages[i];
        texture->allocation = VK_NULL_HANDLE;
        texture->format = sc->format;
        texture->width = width;
        texture->height = height;
        texture->depth = 1;
        texture->mipLevels = 1;
        texture->arrayLayers = 1;
        texture->ownedBySwapchain = true;
        sc->images[sc->imageCount++] = handle;
    }
    return true;
}

FluxionRHISwapchainHandle Fluxion_RHIVulkan_CreateSwapchain(FluxionRHIDeviceHandle device, FluxionWindowHandle window, const FluxionRHISwapchainDesc* desc)
{
    FluxionRHISwapchainHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    if (deviceState == nullptr || desc == nullptr || !FLUXION_HANDLE_IS_VALID(window)) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIVulkan_PoolAllocate(s_swapchainSlots, FLUXION_RHI_VULKAN_MAX_SWAPCHAINS, &index, &generation)) return invalid;

    FluxionRHIVulkanSwapchain* sc = &s_swapchains[index];
    *sc = FluxionRHIVulkanSwapchain{};
    sc->window = window;
    sc->requestedBufferCount = desc->bufferCount > 0 ? desc->bufferCount : 2;
    sc->vsync = desc->vsync;
    sc->format = desc->format != FLUXION_RHI_FORMAT_UNKNOWN ? Fluxion_RHIVulkan_MapFormat(desc->format) : VK_FORMAT_B8G8R8A8_UNORM;
    sc->presentMode = desc->vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;

    sc->surface = Fluxion_RHIVulkan_CreateSurface(window);
    if (sc->surface == VK_NULL_HANDLE)
    {
        Fluxion_RHIVulkan_PoolFree(s_swapchainSlots, FLUXION_RHI_VULKAN_MAX_SWAPCHAINS, index, generation);
        return invalid;
    }

    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(deviceState->physicalDevice, deviceState->graphicsFamily, sc->surface, &supported);
    if (!supported)
    {
        vkDestroySurfaceKHR(Fluxion_RHIVulkan_GetInstance(), sc->surface, nullptr);
        Fluxion_RHIVulkan_PoolFree(s_swapchainSlots, FLUXION_RHI_VULKAN_MAX_SWAPCHAINS, index, generation);
        return invalid;
    }

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(deviceState->device, &fenceInfo, nullptr, &sc->acquireFence);

    if (!Fluxion_RHIVulkan_BuildSwapchain(deviceState, sc))
    {
        vkDestroyFence(deviceState->device, sc->acquireFence, nullptr);
        vkDestroySurfaceKHR(Fluxion_RHIVulkan_GetInstance(), sc->surface, nullptr);
        Fluxion_RHIVulkan_PoolFree(s_swapchainSlots, FLUXION_RHI_VULKAN_MAX_SWAPCHAINS, index, generation);
        return invalid;
    }

    FluxionRHISwapchainHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIVulkan_DestroySwapchain(FluxionRHISwapchainHandle swapchain)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_swapchainSlots, FLUXION_RHI_VULKAN_MAX_SWAPCHAINS, swapchain.index, swapchain.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: destroy called with an invalid or already-destroyed swapchain handle");
        return;
    }
    FluxionRHIVulkanSwapchain* sc = &s_swapchains[swapchain.index];
    VkDevice device = Fluxion_RHIVulkan_GetOwningDevice();
    vkDeviceWaitIdle(device);
    Fluxion_RHIVulkan_DestroySwapchainImages(sc);
    if (sc->swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, sc->swapchain, nullptr);
    if (sc->acquireFence != VK_NULL_HANDLE) vkDestroyFence(device, sc->acquireFence, nullptr);
    if (sc->surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(Fluxion_RHIVulkan_GetInstance(), sc->surface, nullptr);
    *sc = FluxionRHIVulkanSwapchain{};
    Fluxion_RHIVulkan_PoolFree(s_swapchainSlots, FLUXION_RHI_VULKAN_MAX_SWAPCHAINS, swapchain.index, swapchain.generation);
}

u32 Fluxion_RHIVulkan_SwapchainAcquireNextImage(FluxionRHISwapchainHandle swapchain, FluxionRHISemaphoreHandle signalSemaphore)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_swapchainSlots, FLUXION_RHI_VULKAN_MAX_SWAPCHAINS, swapchain.index, swapchain.generation)) return 0;
    FluxionRHIVulkanSwapchain* sc = &s_swapchains[swapchain.index];
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_SoleDevice();
    if (deviceState == nullptr) return 0;

    VkSemaphore vkSemaphore = Fluxion_RHIVulkan_ResolveBinarySemaphore(signalSemaphore);

    for (u32 attempt = 0; attempt < 2; ++attempt)
    {
        if (sc->swapchain == VK_NULL_HANDLE)
        {
            if (!Fluxion_RHIVulkan_BuildSwapchain(deviceState, sc)) return 0; // minimized or failed -- caller should skip this frame
        }

        u32 imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(deviceState->device, sc->swapchain, UINT64_MAX, vkSemaphore, sc->acquireFence, &imageIndex);
        if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
        {
            vkWaitForFences(deviceState->device, 1, &sc->acquireFence, VK_TRUE, UINT64_MAX);
            vkResetFences(deviceState->device, 1, &sc->acquireFence);
            return imageIndex;
        }
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            vkDeviceWaitIdle(deviceState->device);
            vkDestroySwapchainKHR(deviceState->device, sc->swapchain, nullptr);
            Fluxion_RHIVulkan_DestroySwapchainImages(sc);
            sc->swapchain = VK_NULL_HANDLE;
            continue; // retry once against the freshly rebuilt swapchain
        }
        return 0;
    }
    return 0;
}

FluxionRHITextureHandle Fluxion_RHIVulkan_SwapchainGetTexture(FluxionRHISwapchainHandle swapchain, u32 imageIndex)
{
    FluxionRHITextureHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!Fluxion_RHIVulkan_PoolIsValid(s_swapchainSlots, FLUXION_RHI_VULKAN_MAX_SWAPCHAINS, swapchain.index, swapchain.generation)) return invalid;
    FluxionRHIVulkanSwapchain* sc = &s_swapchains[swapchain.index];
    if (imageIndex >= sc->imageCount) return invalid;
    return sc->images[imageIndex];
}

void Fluxion_RHIVulkan_SwapchainPresent(FluxionRHISwapchainHandle swapchain, u32 imageIndex, FluxionRHISemaphoreHandle waitSemaphore)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_swapchainSlots, FLUXION_RHI_VULKAN_MAX_SWAPCHAINS, swapchain.index, swapchain.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: Present called with an invalid swapchain handle");
        return;
    }
    FluxionRHIVulkanSwapchain* sc = &s_swapchains[swapchain.index];
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_SoleDevice();
    if (deviceState == nullptr || sc->swapchain == VK_NULL_HANDLE) return;

    VkSemaphore vkSemaphore = Fluxion_RHIVulkan_ResolveBinarySemaphore(waitSemaphore);

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    if (vkSemaphore != VK_NULL_HANDLE)
    {
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &vkSemaphore;
    }
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &sc->swapchain;
    presentInfo.pImageIndices = &imageIndex;

    VkResult result = vkQueuePresentKHR(Fluxion_RHIVulkan_ResolvePresentQueue(deviceState), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        // Present this frame already happened (or the driver tolerated
        // it); rebuild lazily on the next Acquire rather than here.
        vkDeviceWaitIdle(deviceState->device);
        vkDestroySwapchainKHR(deviceState->device, sc->swapchain, nullptr);
        Fluxion_RHIVulkan_DestroySwapchainImages(sc);
        sc->swapchain = VK_NULL_HANDLE;
    }
}

void Fluxion_RHIVulkan_SwapchainGetExtent(FluxionRHISwapchainHandle swapchain, u32* outWidth, u32* outHeight)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_swapchainSlots, FLUXION_RHI_VULKAN_MAX_SWAPCHAINS, swapchain.index, swapchain.generation))
    {
        if (outWidth) *outWidth = 0;
        if (outHeight) *outHeight = 0;
        return;
    }
    FluxionRHIVulkanSwapchain* sc = &s_swapchains[swapchain.index];

    // With no swapchain object there is no extent to report, and the
    // last one's size is the worst possible answer: it looks entirely
    // ordinary, so a caller sizes its render area and its own attachments
    // to a surface that does not exist and draws into nothing. Reporting
    // zero is what lets "skip this frame" be noticed at all.
    if (sc->swapchain == VK_NULL_HANDLE)
    {
        if (outWidth) *outWidth = 0;
        if (outHeight) *outHeight = 0;
        return;
    }

    if (outWidth) *outWidth = sc->width;
    if (outHeight) *outHeight = sc->height;
}
