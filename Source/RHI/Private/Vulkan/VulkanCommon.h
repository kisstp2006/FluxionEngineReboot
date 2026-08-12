#pragma once

// Module-private, Vulkan-backend-private: shared between every
// Private/Vulkan/*.cpp translation unit, never included from Include/ or
// from the Null backend. Compiled as C++ (not C, unlike NullBackend.c)
// because VMA's implementation is C++ -- see ThirdParty/vma/vma_impl.cpp.

#include "../RHIBackendVTable.h"

#include <Fluxion/Foundation/Assert.h>

// The platform macro must be defined, and the platform's own header
// included, BEFORE the first #include of vulkan/vulkan.h -- that's what
// makes vulkan.h pull in the matching vulkan_win32.h/vulkan_xlib.h (from
// the same include root, so there's no separate, ambiguous angle-bracket
// lookup for it) with HWND/HINSTANCE/Display/Window already visible.
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#define VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xlib.h>
#endif

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#define FLUXION_RHI_VULKAN_MAX_DEVICES 2
#define FLUXION_RHI_VULKAN_MAX_ADAPTERS 16
#define FLUXION_RHI_VULKAN_MAX_COMMAND_LISTS 64
#define FLUXION_RHI_VULKAN_MAX_BUFFERS 1024
#define FLUXION_RHI_VULKAN_MAX_TEXTURES 1024
#define FLUXION_RHI_VULKAN_MAX_TEXTURE_VIEWS 1024
#define FLUXION_RHI_VULKAN_MAX_SAMPLERS 64
#define FLUXION_RHI_VULKAN_MAX_SHADERS 128
#define FLUXION_RHI_VULKAN_MAX_PIPELINES 128
#define FLUXION_RHI_VULKAN_MAX_SWAPCHAINS 4
#define FLUXION_RHI_VULKAN_MAX_SWAPCHAIN_IMAGES 4
#define FLUXION_RHI_VULKAN_MAX_FENCES 64
#define FLUXION_RHI_VULKAN_MAX_SEMAPHORES 64
#define FLUXION_RHI_VULKAN_MAX_QUERY_POOLS 16
#define FLUXION_RHI_VULKAN_MAX_RETIRED 512
#define FLUXION_RHI_VULKAN_MAX_BIND_GROUP_LAYOUTS 128
#define FLUXION_RHI_VULKAN_MAX_BIND_GROUPS 512

// --- Generic slot pool (same alive+generation idiom as NullBackend.c) ------

struct FluxionRHIVulkanSlot
{
    bool alive;
    u32 generation;
};

bool Fluxion_RHIVulkan_PoolAllocate(FluxionRHIVulkanSlot* slots, u32 capacity, u32* outIndex, u32* outGeneration);
bool Fluxion_RHIVulkan_PoolIsValid(const FluxionRHIVulkanSlot* slots, u32 capacity, u32 index, u32 generation);
void Fluxion_RHIVulkan_PoolFree(FluxionRHIVulkanSlot* slots, u32 capacity, u32 index, u32 generation);

// --- Device (shared by every other Vulkan/*.cpp) ----------------------------
//
// GPU resources can't always be freed the instant Destroy* is called --
// the GPU might still be reading them from an earlier submission. Each
// device keeps one retirement queue, drained by CollectGarbage once it
// can prove the GPU has moved past the point where the resource was
// still needed. That proof comes from a device-internal timeline
// semaphore that every Queue_Submit signals purely for this bookkeeping,
// kept separate from any user-visible FluxionRHIFenceHandle so a caller
// that never asks for a fence still gets correct deferred destruction.

struct FluxionRHIVulkanRetiredEntry
{
    u64 retireAfterValue; // safe to actually destroy once s_gcTimeline reaches this
    enum class Kind { Buffer, Texture, TextureView, Sampler, Shader, Pipeline, BindGroup } kind;
    u32 index; // index into the owning pool
};

struct FluxionRHIVulkanDevice
{
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;

    u32 graphicsFamily = 0;
    u32 computeFamily = 0;
    u32 transferFamily = 0;
    bool hasDedicatedCompute = false;
    bool hasDedicatedTransfer = false;

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    VkQueue transferQueue = VK_NULL_HANDLE;

    // Resolved once at device-creation time to whichever of the core-1.3
    // or KHR-extension entry point actually exists on this device -- the
    // KHR PFN typedefs are used for both cases since a promoted-to-core
    // function keeps an identical signature, so calling through either
    // name via the same pointer type is safe.
    PFN_vkCmdBeginRenderingKHR cmdBeginRendering = nullptr;
    PFN_vkCmdEndRenderingKHR cmdEndRendering = nullptr;
    PFN_vkQueueSubmit2KHR queueSubmit2 = nullptr;
    PFN_vkCmdPipelineBarrier2KHR cmdPipelineBarrier2 = nullptr;

    // GC-only timeline semaphore, purely internal bookkeeping -- never
    // exposed to callers.
    VkSemaphore gcTimeline = VK_NULL_HANDLE;
    u64 gcCounter = 0;

    FluxionRHIVulkanRetiredEntry retired[FLUXION_RHI_VULKAN_MAX_RETIRED];
    u32 retiredCount = 0;

    // One process-lifetime cache shared by every graphics/compute pipeline
    // this device creates -- see Fluxion_RHI_Device_Save/LoadPipelineCacheToFile.
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;

    // Backs every FluxionRHIBindGroup allocated from a BindGroupLayout
    // created on this device -- FREE_DESCRIPTOR_SET_BIT so an individual
    // BindGroup can be freed (via the same retirement queue as every
    // other resource here) without resetting the whole pool.
    VkDescriptorPool bindGroupPool = VK_NULL_HANDLE;

    // A zero-binding VkDescriptorSetLayout, lazily created and reused to
    // fill any FLUXION_RHI_BIND_GROUP_* index a pipeline desc leaves as
    // an invalid handle -- VkPipelineLayoutCreateInfo's pSetLayouts array
    // has no concept of "this set index is unused", so every gap between
    // the frequencies a shader actually uses still needs a real (just
    // empty) layout object at that index.
    VkDescriptorSetLayout emptyBindGroupLayout = VK_NULL_HANDLE;
};

FluxionRHIVulkanDevice* Fluxion_RHIVulkan_ResolveDevice(FluxionRHIDeviceHandle device);
VkInstance Fluxion_RHIVulkan_GetInstance(void);
VkPhysicalDevice Fluxion_RHIVulkan_ResolvePhysicalDevice(FluxionRHIAdapterHandle adapter);

// This backend only ever has one active device at a time (mirrors RHI.c's
// own "one active backend at a time" assumption for the whole module) --
// these three let a resource pool that outlives any one CreateDevice/
// DestroyDevice call (Buffer/Texture/.../Pipeline are all file-scope pools,
// not per-device) reach the allocator/VkDevice that owns it without every
// pool having to track a per-slot owning-device back-reference.
FluxionRHIVulkanDevice* Fluxion_RHIVulkan_SoleDevice(void);
VkDevice Fluxion_RHIVulkan_GetOwningDevice(void);
VmaAllocator Fluxion_RHIVulkan_GetOwningAllocator(void);

// Bumps and returns the value the *next* submission should signal
// s_gcTimeline to -- called once per Queue_Submit.
u64 Fluxion_RHIVulkan_NextGCValue(FluxionRHIVulkanDevice* deviceState);
// Marks a pool entry for deferred destruction once s_gcTimeline reaches
// `retireAfterValue` (normally the device's current/most-recent GC value
// -- always safe to wait for, if not maximally tight, since any earlier
// submission has necessarily completed by the time a later one has).
void Fluxion_RHIVulkan_Retire(FluxionRHIVulkanDevice* deviceState, FluxionRHIVulkanRetiredEntry::Kind kind, u32 index, u64 retireAfterValue);
void Fluxion_RHIVulkan_CollectGarbage(FluxionRHIDeviceHandle device);

// --- Resource pools (defined in VulkanMemory.cpp / VulkanShader.cpp / etc,
// resolved from other translation units through these accessors) ----------

struct FluxionRHIVulkanBuffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    usize size = 0;
    void* mappedPointer = nullptr; // non-null only for a CPU-visible memory class (VMA_ALLOCATION_CREATE_MAPPED_BIT)
};

struct FluxionRHIVulkanTexture
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE; // VK_NULL_HANDLE for swapchain-owned images
    VkFormat format = VK_FORMAT_UNDEFINED;
    u32 width = 0, height = 0, depth = 1;
    u32 mipLevels = 1, arrayLayers = 1;
    bool ownedBySwapchain = false;
};

struct FluxionRHIVulkanTextureView
{
    VkImageView view = VK_NULL_HANDLE;
    FluxionRHITextureHandle texture{};
};

FluxionRHIBufferHandle Fluxion_RHIVulkan_CreateBuffer(FluxionRHIDeviceHandle device, const FluxionRHIBufferDesc* desc);
void Fluxion_RHIVulkan_DestroyBuffer(FluxionRHIBufferHandle buffer);
void* Fluxion_RHIVulkan_MapBuffer(FluxionRHIBufferHandle buffer);
void Fluxion_RHIVulkan_UnmapBuffer(FluxionRHIBufferHandle buffer);
FluxionRHIVulkanBuffer* Fluxion_RHIVulkan_ResolveBuffer(FluxionRHIBufferHandle buffer);

FluxionRHITextureHandle Fluxion_RHIVulkan_CreateTexture(FluxionRHIDeviceHandle device, const FluxionRHITextureDesc* desc);
void Fluxion_RHIVulkan_DestroyTexture(FluxionRHITextureHandle texture);
FluxionRHIVulkanTexture* Fluxion_RHIVulkan_ResolveTexture(FluxionRHITextureHandle texture);
// Used by the swapchain (owns its images outright, bypassing normal Create).
bool Fluxion_RHIVulkan_AllocateTextureSlot(FluxionRHITextureHandle* outHandle, u32* outIndex);
void Fluxion_RHIVulkan_FreeTextureSlotDirect(FluxionRHITextureHandle handle);

FluxionRHITextureViewHandle Fluxion_RHIVulkan_CreateTextureView(FluxionRHIDeviceHandle device, const FluxionRHITextureViewDesc* desc);
void Fluxion_RHIVulkan_DestroyTextureView(FluxionRHITextureViewHandle view);
FluxionRHIVulkanTextureView* Fluxion_RHIVulkan_ResolveTextureView(FluxionRHITextureViewHandle view);

FluxionRHISamplerHandle Fluxion_RHIVulkan_CreateSampler(FluxionRHIDeviceHandle device, const FluxionRHISamplerDesc* desc);
void Fluxion_RHIVulkan_DestroySampler(FluxionRHISamplerHandle sampler);
VkSampler Fluxion_RHIVulkan_ResolveSampler(FluxionRHISamplerHandle sampler);

FluxionRHIShaderHandle Fluxion_RHIVulkan_CreateShader(FluxionRHIDeviceHandle device, const FluxionRHIShaderDesc* desc);
void Fluxion_RHIVulkan_DestroyShader(FluxionRHIShaderHandle shader);
VkShaderModule Fluxion_RHIVulkan_ResolveShaderModule(FluxionRHIShaderHandle shader);
VkShaderStageFlagBits Fluxion_RHIVulkan_ResolveShaderStage(FluxionRHIShaderHandle shader);
const char* Fluxion_RHIVulkan_ResolveShaderEntryPoint(FluxionRHIShaderHandle shader);

struct FluxionRHIVulkanPipeline
{
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
};

FluxionRHIPipelineHandle Fluxion_RHIVulkan_CreateGraphicsPipeline(FluxionRHIDeviceHandle device, const FluxionRHIGraphicsPipelineDesc* desc);
FluxionRHIPipelineHandle Fluxion_RHIVulkan_CreateComputePipeline(FluxionRHIDeviceHandle device, const FluxionRHIComputePipelineDesc* desc);
void Fluxion_RHIVulkan_DestroyPipeline(FluxionRHIPipelineHandle pipeline);
FluxionRHIVulkanPipeline* Fluxion_RHIVulkan_ResolvePipeline(FluxionRHIPipelineHandle pipeline);

bool Fluxion_RHIVulkan_SavePipelineCacheToFile(FluxionRHIDeviceHandle device, const char* path);
bool Fluxion_RHIVulkan_LoadPipelineCacheFromFile(FluxionRHIDeviceHandle device, const char* path);

// --- Binding model (VulkanBindGroup.cpp) ------------------------------------

// Deduplicated by binding-list shape (a device-scoped cache keyed by a
// hash of the entry list) -- two FluxionRHIBindGroupLayoutDesc calls
// describing the same shape return the same underlying
// VkDescriptorSetLayout and the same FluxionRHIBindGroupLayoutHandle.
FluxionRHIBindGroupLayoutHandle Fluxion_RHIVulkan_CreateBindGroupLayout(FluxionRHIDeviceHandle device, const FluxionRHIBindGroupLayoutDesc* desc);
void Fluxion_RHIVulkan_DestroyBindGroupLayout(FluxionRHIBindGroupLayoutHandle layout);
VkDescriptorSetLayout Fluxion_RHIVulkan_ResolveBindGroupLayout(FluxionRHIBindGroupLayoutHandle layout);

FluxionRHIBindGroupHandle Fluxion_RHIVulkan_CreateBindGroup(FluxionRHIDeviceHandle device, const FluxionRHIBindGroupDesc* desc);
void Fluxion_RHIVulkan_DestroyBindGroup(FluxionRHIBindGroupHandle bindGroup);
VkDescriptorSet Fluxion_RHIVulkan_ResolveBindGroup(FluxionRHIBindGroupHandle bindGroup);
void Fluxion_RHIVulkan_FinalizeBindGroupSlot(u32 index);

// Lazily creates (once per device) and returns deviceState->emptyBindGroupLayout.
VkDescriptorSetLayout Fluxion_RHIVulkan_GetOrCreateEmptyBindGroupLayout(FluxionRHIVulkanDevice* deviceState);

// --- Command lists (VulkanCommandList.cpp) ----------------------------------

FluxionRHICommandListHandle Fluxion_RHIVulkan_CreateCommandList(FluxionRHIDeviceHandle device, FluxionRHIQueueType type);
void Fluxion_RHIVulkan_DestroyCommandList(FluxionRHICommandListHandle commandList);
VkCommandBuffer Fluxion_RHIVulkan_ResolveCommandBuffer(FluxionRHICommandListHandle commandList);
FluxionRHIQueueType Fluxion_RHIVulkan_ResolveCommandListQueueType(FluxionRHICommandListHandle commandList);

void Fluxion_RHIVulkan_CommandListCopyBufferToTexture(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle src, usize srcOffset, FluxionRHITextureHandle dst, u32 mipLevel, u32 arrayLayer);

// Portable resource state -> Vulkan (layout, access2, stage2) triple --
// shared by CommandList barriers and by resource creation (initial
// layout) / swapchain present transitions.
void Fluxion_RHIVulkan_MapResourceState(FluxionRHIResourceState state, VkImageLayout* outLayout, VkAccessFlags2* outAccess, VkPipelineStageFlags2* outStage);
VkFormat Fluxion_RHIVulkan_MapFormat(FluxionRHIFormat format);
FluxionRHIFormat Fluxion_RHIVulkan_MapFormatBack(VkFormat format);

// --- Sync (VulkanSync.cpp) --------------------------------------------------

FluxionRHIFenceHandle Fluxion_RHIVulkan_CreateFence(FluxionRHIDeviceHandle device, bool signaled);
void Fluxion_RHIVulkan_DestroyFence(FluxionRHIFenceHandle fence);
bool Fluxion_RHIVulkan_WaitForFence(FluxionRHIFenceHandle fence);
void Fluxion_RHIVulkan_ResetFence(FluxionRHIFenceHandle fence);
// Called once by QueueSubmit: returns the timeline semaphore + value the
// submit should add to its signal list for this fence (or VK_NULL_HANDLE
// if the handle is invalid).
VkSemaphore Fluxion_RHIVulkan_FenceBeginSignal(FluxionRHIFenceHandle fence, u64* outValue);

FluxionRHISemaphoreHandle Fluxion_RHIVulkan_CreateSemaphore(FluxionRHIDeviceHandle device);
void Fluxion_RHIVulkan_DestroySemaphore(FluxionRHISemaphoreHandle semaphore);
VkSemaphore Fluxion_RHIVulkan_ResolveBinarySemaphore(FluxionRHISemaphoreHandle semaphore);

FluxionRHIQueryPoolHandle Fluxion_RHIVulkan_CreateQueryPool(FluxionRHIDeviceHandle device, u32 queryCount);
void Fluxion_RHIVulkan_DestroyQueryPool(FluxionRHIQueryPoolHandle queryPool);

// --- Upload -----------------------------------------------------------------
//
// The RHI contract's CopyBuffer/CopyTexture only ever move data GPU-side
// (both arguments are already-created RHI resources) -- the actual CPU
// entry point is Fluxion_RHI_MapBuffer on a CPU_TO_GPU buffer. So
// "upload" is just the standard explicit Vulkan staging-buffer pattern
// at the caller's level: create a CPU_TO_GPU buffer, Map, write, Unmap,
// then CommandList CopyBuffer into a GPU_ONLY destination -- no hidden
// backend-owned ring buffer or dedicated-transfer-queue auto-routing is
// needed for that to work correctly (a caller that wants the transfer
// queue specifically can already ask for it via
// Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_TRANSFER)).

// --- Swapchain (VulkanSwapchain.cpp) ----------------------------------------

FluxionRHISwapchainHandle Fluxion_RHIVulkan_CreateSwapchain(FluxionRHIDeviceHandle device, FluxionWindowHandle window, const FluxionRHISwapchainDesc* desc);
void Fluxion_RHIVulkan_DestroySwapchain(FluxionRHISwapchainHandle swapchain);
u32 Fluxion_RHIVulkan_SwapchainAcquireNextImage(FluxionRHISwapchainHandle swapchain, FluxionRHISemaphoreHandle signalSemaphore);
FluxionRHITextureHandle Fluxion_RHIVulkan_SwapchainGetTexture(FluxionRHISwapchainHandle swapchain, u32 imageIndex);
void Fluxion_RHIVulkan_SwapchainPresent(FluxionRHISwapchainHandle swapchain, u32 imageIndex, FluxionRHISemaphoreHandle waitSemaphore);
void Fluxion_RHIVulkan_SwapchainGetExtent(FluxionRHISwapchainHandle swapchain, u32* outWidth, u32* outHeight);
VkQueue Fluxion_RHIVulkan_ResolvePresentQueue(FluxionRHIVulkanDevice* deviceState);

#define FLUXION_RHIVULKAN_CHECK(expr) do { VkResult fluxionVkResult_ = (expr); FLUXION_ASSERT_MSG(fluxionVkResult_ == VK_SUCCESS, #expr " failed"); } while (0)
