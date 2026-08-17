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

#pragma once

// Module-private, D3D12-backend-private: shared between every
// Private/D3D12/*.cpp translation unit, never included from Include/ or
// from any other backend. Compiled as C++ (not C, unlike NullBackend.c)
// because D3D12MA's implementation is C++ -- see
// ThirdParty/d3d12ma/D3D12MemAlloc.cpp. Windows-only: this whole
// directory is excluded from the build on non-Windows platforms (see
// Source/RHI/CMakeLists.txt).

#include "../RHIBackendVTable.h"

#include <Fluxion/Foundation/Assert.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <D3D12MemAlloc.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <vector>

using Microsoft::WRL::ComPtr;

#define FLUXION_RHI_D3D12_MAX_DEVICES 2
#define FLUXION_RHI_D3D12_MAX_ADAPTERS 16
#define FLUXION_RHI_D3D12_MAX_COMMAND_LISTS 64
#define FLUXION_RHI_D3D12_MAX_BUFFERS 1024
#define FLUXION_RHI_D3D12_MAX_TEXTURES 1024
#define FLUXION_RHI_D3D12_MAX_TEXTURE_VIEWS 1024
#define FLUXION_RHI_D3D12_MAX_SAMPLERS 64
#define FLUXION_RHI_D3D12_MAX_SHADERS 128
#define FLUXION_RHI_D3D12_MAX_PIPELINES 128
#define FLUXION_RHI_D3D12_MAX_SWAPCHAINS 4
#define FLUXION_RHI_D3D12_MAX_SWAPCHAIN_IMAGES 4
#define FLUXION_RHI_D3D12_MAX_FENCES 64
#define FLUXION_RHI_D3D12_MAX_SEMAPHORES 64
#define FLUXION_RHI_D3D12_MAX_QUERY_POOLS 16
#define FLUXION_RHI_D3D12_MAX_RETIRED 512
#define FLUXION_RHI_D3D12_MAX_BIND_GROUP_LAYOUTS 128
#define FLUXION_RHI_D3D12_MAX_BIND_GROUPS 512

// Sized generously for a v1 free-list heap (see FluxionRHID3D12HeapAllocator
// below) -- large enough for many BindGroups' worth of CBV/SRV/UAV and
// sampler descriptors without needing to grow at runtime. D3D12's own
// hard cap for a shader-visible CBV_SRV_UAV heap is far larger (1,000,000
// on tier-3-resource-binding hardware), so this is a v1-scoped budget,
// not a driver limit.
#define FLUXION_RHI_D3D12_CBV_SRV_UAV_HEAP_SIZE 4096
#define FLUXION_RHI_D3D12_SAMPLER_HEAP_SIZE 256

// --- Generic slot pool (same alive+generation idiom as NullBackend.c/VulkanCommon.h) --

struct FluxionRHID3D12Slot
{
    bool alive;
    u32 generation;

    // Set between a destroy and the moment the GPU is known to be done
    // with what lived here. The handle is already dead by then -- the
    // generation moved on -- but the slot must not be handed out again,
    // because the object it names has not been let go of yet and the
    // storage still holds it. Handing it out would put a new object where
    // a queued release is still pointing, and that release would then
    // free the new one.
    bool pendingFinalize;
};

bool Fluxion_RHID3D12_PoolAllocate(FluxionRHID3D12Slot* slots, u32 capacity, u32* outIndex, u32* outGeneration);
// Ends a handle's life without freeing its slot; the slot comes back only
// at PoolFinalize, once the queued release has actually run.
void Fluxion_RHID3D12_PoolRetire(FluxionRHID3D12Slot* slots, u32 capacity, u32 index, u32 generation);
void Fluxion_RHID3D12_PoolFinalize(FluxionRHID3D12Slot* slots, u32 index);
bool Fluxion_RHID3D12_PoolIsValid(const FluxionRHID3D12Slot* slots, u32 capacity, u32 index, u32 generation);
void Fluxion_RHID3D12_PoolFree(FluxionRHID3D12Slot* slots, u32 capacity, u32 index, u32 generation);

// --- Descriptor heap free-list allocator (D3D12Binding.cpp) -----------------
//
// A v1 first-fit free list over one fixed-size range of a shader-visible
// heap -- simpler than a real slab/buddy allocator, matching this
// engine's established "simplicity wins in v1" precedent (the Vulkan
// backend's retirement-queue-based BindGroup destruction instead of a
// frame-scoped bump allocator is the same trade-off). Fragmentation is
// an accepted, documented v1 cost.
struct FluxionRHID3D12HeapRange
{
    u32 offset;
    u32 count;
};

struct FluxionRHID3D12HeapAllocator
{
    ID3D12DescriptorHeap* heap = nullptr;
    u32 capacity = 0;
    u32 descriptorSize = 0;
    FluxionRHID3D12HeapRange freeRanges[64];
    u32 freeRangeCount = 0;
};

bool Fluxion_RHID3D12_HeapAllocatorInit(FluxionRHID3D12HeapAllocator* allocator, ID3D12DescriptorHeap* heap, u32 capacity, u32 descriptorSize);
bool Fluxion_RHID3D12_HeapAllocatorAllocate(FluxionRHID3D12HeapAllocator* allocator, u32 count, u32* outOffset);
void Fluxion_RHID3D12_HeapAllocatorFree(FluxionRHID3D12HeapAllocator* allocator, u32 offset, u32 count);
D3D12_CPU_DESCRIPTOR_HANDLE Fluxion_RHID3D12_HeapCpuHandle(const FluxionRHID3D12HeapAllocator* allocator, u32 offset);
D3D12_GPU_DESCRIPTOR_HANDLE Fluxion_RHID3D12_HeapGpuHandle(const FluxionRHID3D12HeapAllocator* allocator, ID3D12DescriptorHeap* gpuHeap, u32 offset);

// --- Device (shared by every other D3D12/*.cpp) -----------------------------
//
// Same deferred-destruction reasoning as VulkanCommon.h's retirement
// queue: a resource's GPU memory can't always be freed the instant
// Destroy* is called, since an earlier command list submission might
// still be executing against it. One monotonically-increasing
// ID3D12Fence per device plays the exact role Vulkan's gcTimeline
// semaphore does -- D3D12 fences are natively timeline-style (a single
// UINT64 counter), so the mechanism carries over almost unchanged.

struct FluxionRHID3D12RetiredEntry
{
    u64 retireAfterValue; // safe to actually destroy once gcFence reaches this
    enum class Kind { Buffer, Texture, TextureView, Sampler, Shader, Pipeline, BindGroup } kind;
    u32 index; // index into the owning pool
};

struct FluxionRHID3D12Device
{
    ComPtr<IDXGIAdapter1> adapter;
    // ID3D12Device1 (not the base ID3D12Device) so CreatePipelineLibrary
    // is available directly -- introduced in the Windows 10 1607 SDK,
    // universally present on any machine with a current D3D12 driver, so
    // this isn't a meaningfully narrower device-creation requirement than
    // requesting the base interface would be.
    ComPtr<ID3D12Device1> device;
    ComPtr<D3D12MA::Allocator> allocator;

    ComPtr<ID3D12CommandQueue> graphicsQueue;
    ComPtr<ID3D12CommandQueue> computeQueue;   // aliases graphicsQueue: this engine's D3D12 backend
    ComPtr<ID3D12CommandQueue> transferQueue;  // never requests dedicated compute/copy queues in v1

    // GC-only fence, purely internal bookkeeping -- never exposed to
    // callers (mirrors FluxionRHIVulkanDevice::gcTimeline).
    ComPtr<ID3D12Fence> gcFence;
    HANDLE gcEvent = nullptr;
    u64 gcCounter = 0;

    FluxionRHID3D12RetiredEntry retired[FLUXION_RHI_D3D12_MAX_RETIRED];
    u32 retiredCount = 0;

    // One process-lifetime ID3D12PipelineLibrary shared by every
    // graphics/compute PSO this device creates -- see
    // Fluxion_RHID3D12_SavePipelineCacheToFile/LoadPipelineCacheFromFile.
    ComPtr<ID3D12PipelineLibrary> pipelineLibrary;

    // The blob the library above was created from. D3D12 does not copy
    // it: the library reads out of this memory for as long as it lives,
    // so it has to outlive the library rather than the call that loaded
    // it. Freeing it early leaves a library that still answers, out of
    // memory that is no longer ours -- which shows up much later, as a
    // save that quietly produces nothing.
    std::vector<u8> pipelineLibraryBlob;

    // Shader-visible heaps backing every FluxionRHIBindGroup allocated on
    // this device -- one persistent CBV_SRV_UAV heap, one persistent
    // Sampler heap (D3D12 requires these to be separate heaps), each with
    // its own free-list allocator.
    ComPtr<ID3D12DescriptorHeap> cbvSrvUavHeap;
    ComPtr<ID3D12DescriptorHeap> samplerHeap;
    FluxionRHID3D12HeapAllocator cbvSrvUavAllocator;
    FluxionRHID3D12HeapAllocator samplerAllocator;

    // Non-shader-visible: RTV/DSV descriptors are read directly by
    // OMSetRenderTargets at record time (never indexed from a shader),
    // so they never need to live in a shader-visible heap. One slot per
    // FluxionRHITextureView pool index (direct-mapped, not a separate
    // free-list allocator) -- a view's owning texture's usage flags
    // decide whether its RTV and/or DSV slot is ever actually written.
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    u32 rtvDescriptorSize = 0;
    u32 dsvDescriptorSize = 0;
};

FluxionRHID3D12Device* Fluxion_RHID3D12_ResolveDevice(FluxionRHIDeviceHandle device);
IDXGIFactory6* Fluxion_RHID3D12_GetFactory(void);
IDXGIAdapter1* Fluxion_RHID3D12_ResolveAdapter(FluxionRHIAdapterHandle adapter);

// Attaches a caller-supplied name to a native object, so PIX shows
// "DemoAlbedoTexture" instead of a bare pointer. NULL or empty names are
// quietly ignored -- a name is diagnostic freight, never something an
// object's creation may fail over. Samplers and shaders have no
// ID3D12Object of their own in this API (a sampler is a descriptor, a
// shader is bytecode), so those two names have nowhere to go here.
void Fluxion_RHID3D12_SetName(ID3D12Object* object, const char* name);

// This backend only ever has one active device at a time (mirrors
// VulkanCommon.h's identical "one active device" assumption) -- these
// let a resource pool that outlives any one CreateDevice/DestroyDevice
// call reach the ID3D12Device/allocator that owns it.
FluxionRHID3D12Device* Fluxion_RHID3D12_SoleDevice(void);
ID3D12Device* Fluxion_RHID3D12_GetOwningDevice(void);
D3D12MA::Allocator* Fluxion_RHID3D12_GetOwningAllocator(void);

// Bumps and returns the value the *next* submission should signal
// gcFence to -- called once per Queue_Submit.
u64 Fluxion_RHID3D12_NextGCValue(FluxionRHID3D12Device* deviceState);
void Fluxion_RHID3D12_Retire(FluxionRHID3D12Device* deviceState, FluxionRHID3D12RetiredEntry::Kind kind, u32 index, u64 retireAfterValue);
void Fluxion_RHID3D12_CollectGarbage(FluxionRHIDeviceHandle device);

// --- Resource pools (defined in D3D12Resources.cpp / D3D12Pipeline.cpp / etc,
// resolved from other translation units through these accessors) -----------

struct FluxionRHID3D12Buffer
{
    ComPtr<ID3D12Resource> resource;
    ComPtr<D3D12MA::Allocation> allocation;
    usize size = 0;
    void* mappedPointer = nullptr; // non-null only for a CPU-visible memory class
};

struct FluxionRHID3D12Texture
{
    ComPtr<ID3D12Resource> resource;
    ComPtr<D3D12MA::Allocation> allocation; // null for swapchain-owned images
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    u32 width = 0, height = 0, depth = 1;
    u32 mipLevels = 1, arrayLayers = 1;
    u32 usageFlags = 0; // FLUXION_RHI_TEXTURE_USAGE_* -- decides which view types CreateTextureView eagerly builds
    bool ownedBySwapchain = false;
};

struct FluxionRHID3D12TextureView
{
    FluxionRHITextureHandle texture{};
    FluxionRHIFormat format = FLUXION_RHI_FORMAT_UNKNOWN;
    u32 baseMipLevel = 0, mipLevelCount = 1, baseArrayLayer = 0, arrayLayerCount = 1;

    // Kept because the resource cannot say it. A cube is an ordinary
    // six-slice array here -- the difference is entirely in the view that
    // reads it, and that view is built later, when a bind group is made.
    FluxionRHITextureDimension dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;
    // Set (non-zero size) only when the owning texture's usage flags make
    // that view type meaningful -- see CreateTextureView in
    // D3D12Resources.cpp. Both live in the device's non-shader-visible
    // rtvHeap/dsvHeap, at this view's own pool slot index.
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    bool hasRtv = false;
    bool hasDsv = false;
};

FluxionRHIBufferHandle Fluxion_RHID3D12_CreateBuffer(FluxionRHIDeviceHandle device, const FluxionRHIBufferDesc* desc);
void Fluxion_RHID3D12_DestroyBuffer(FluxionRHIBufferHandle buffer);
void* Fluxion_RHID3D12_MapBuffer(FluxionRHIBufferHandle buffer);
void Fluxion_RHID3D12_UnmapBuffer(FluxionRHIBufferHandle buffer);
FluxionRHID3D12Buffer* Fluxion_RHID3D12_ResolveBuffer(FluxionRHIBufferHandle buffer);

FluxionRHITextureHandle Fluxion_RHID3D12_CreateTexture(FluxionRHIDeviceHandle device, const FluxionRHITextureDesc* desc);
void Fluxion_RHID3D12_DestroyTexture(FluxionRHITextureHandle texture);
FluxionRHID3D12Texture* Fluxion_RHID3D12_ResolveTexture(FluxionRHITextureHandle texture);
bool Fluxion_RHID3D12_AllocateTextureSlot(FluxionRHITextureHandle* outHandle, u32* outIndex);
void Fluxion_RHID3D12_FreeTextureSlotDirect(FluxionRHITextureHandle handle);

FluxionRHITextureViewHandle Fluxion_RHID3D12_CreateTextureView(FluxionRHIDeviceHandle device, const FluxionRHITextureViewDesc* desc);
void Fluxion_RHID3D12_DestroyTextureView(FluxionRHITextureViewHandle view);
FluxionRHID3D12TextureView* Fluxion_RHID3D12_ResolveTextureView(FluxionRHITextureViewHandle view);

FluxionRHISamplerHandle Fluxion_RHID3D12_CreateSampler(FluxionRHIDeviceHandle device, const FluxionRHISamplerDesc* desc);
void Fluxion_RHID3D12_DestroySampler(FluxionRHISamplerHandle sampler);
const FluxionRHISamplerDesc* Fluxion_RHID3D12_ResolveSamplerDesc(FluxionRHISamplerHandle sampler);

FluxionRHIShaderHandle Fluxion_RHID3D12_CreateShader(FluxionRHIDeviceHandle device, const FluxionRHIShaderDesc* desc);
void Fluxion_RHID3D12_DestroyShader(FluxionRHIShaderHandle shader);
struct FluxionRHID3D12Shader
{
    std::vector<u8> dxil;
    FluxionRHIShaderStage stage = FLUXION_RHI_SHADER_STAGE_VERTEX;
};
FluxionRHID3D12Shader* Fluxion_RHID3D12_ResolveShader(FluxionRHIShaderHandle shader);

// --- Binding model (D3D12Binding.cpp) ---------------------------------------
//
// Unlike Vulkan's cached VkDescriptorSetLayout, a D3D12 root signature is
// built once per PIPELINE (from up to 4 BindGroupLayoutHandles together),
// not per individual layout -- so a BindGroupLayout here is just stored
// CPU-side desc data (same reasoning as the OpenGL backend's
// BindGroupLayout, which also has no real native object). A BindGroup,
// by contrast, DOES own real GPU state: a contiguous range of the
// device's shared CBV_SRV_UAV/Sampler heaps, populated at creation time.

FluxionRHIBindGroupLayoutHandle Fluxion_RHID3D12_CreateBindGroupLayout(FluxionRHIDeviceHandle device, const FluxionRHIBindGroupLayoutDesc* desc);
void Fluxion_RHID3D12_DestroyBindGroupLayout(FluxionRHIBindGroupLayoutHandle layout);
const FluxionRHIBindGroupLayoutDesc* Fluxion_RHID3D12_ResolveBindGroupLayoutDesc(FluxionRHIBindGroupLayoutHandle layout);

// Shared by D3D12Binding.cpp (writing a BindGroup's heap) and
// D3D12Pipeline.cpp (sizing root-signature ranges) -- both MUST derive
// identical counts from the same layout desc, or the heap's CBV/SRV/UAV
// blocks land at offsets the root signature does not expect. A
// STORAGE_BUFFER entry counts as BOTH one SRV and one UAV slot: the
// compiler emits it read-only in vertex/fragment and read-write in
// compute, and the same layout may serve both pipeline kinds -- so both
// views always exist, and each pipeline references only the table its
// own root signature declared.
struct FluxionRHID3D12LayoutCounts
{
    u32 cbvCount = 0;
    u32 srvCount = 0; // SAMPLED_TEXTURE + STORAGE_BUFFER entries
    u32 uavCount = 0; // STORAGE_BUFFER entries only
    u32 samplerCount = 0;
};
FluxionRHID3D12LayoutCounts Fluxion_RHID3D12_CountLayoutEntries(const FluxionRHIBindGroupLayoutDesc* layout);

struct FluxionRHID3D12BindGroup
{
    FluxionRHIBindGroupLayoutHandle layout{};
    FluxionRHID3D12HeapRange cbvSrvUavRange{}; // count == 0 if this group has no CBV/SRV/UAV entries
    FluxionRHID3D12HeapRange samplerRange{};   // count == 0 if this group has no sampler entries
};

FluxionRHIBindGroupHandle Fluxion_RHID3D12_CreateBindGroup(FluxionRHIDeviceHandle device, const FluxionRHIBindGroupDesc* desc);
void Fluxion_RHID3D12_DestroyBindGroup(FluxionRHIBindGroupHandle bindGroup);
FluxionRHID3D12BindGroup* Fluxion_RHID3D12_ResolveBindGroup(FluxionRHIBindGroupHandle bindGroup);
void Fluxion_RHID3D12_FinalizeBindGroupSlot(u32 index);

// --- Shaders / pipelines (D3D12Pipeline.cpp) --------------------------------
//
// Root signature layout: for each active FLUXION_RHI_BIND_GROUP_* index
// (0..3), up to two root parameters -- one descriptor table covering
// that group's CBV/SRV/UAV entries (D3D12 allows mixed range types
// within a single table), one descriptor table covering its Sampler
// entries (D3D12 requires samplers in a separate heap/table from
// CBV/SRV/UAV, so this is always a second, independent table). A group
// with zero entries of a given class contributes no root parameter for
// it (D3D12 disallows a zero-size descriptor range). rootParamForGroup
// records which root parameter index (if any) each group's two possible
// tables landed at, so CommandListSetBindGroup knows what to bind.
struct FluxionRHID3D12RootParamSlot
{
    i32 cbvSrvUavRootParam = -1;
    i32 samplerRootParam = -1;
};

struct FluxionRHID3D12Pipeline
{
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> pipelineState;
    D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    u32 vertexStride = 0; // from FluxionRHIVertexLayout::stride, needed by CommandListSetVertexBuffer -- the RHI contract doesn't repeat it per SetVertexBuffer call
    bool isCompute = false;
    FluxionRHID3D12RootParamSlot groupSlots[FLUXION_RHI_MAX_BIND_GROUPS];
};

FluxionRHIPipelineHandle Fluxion_RHID3D12_CreateGraphicsPipeline(FluxionRHIDeviceHandle device, const FluxionRHIGraphicsPipelineDesc* desc);
FluxionRHIPipelineHandle Fluxion_RHID3D12_CreateComputePipeline(FluxionRHIDeviceHandle device, const FluxionRHIComputePipelineDesc* desc);
void Fluxion_RHID3D12_DestroyPipeline(FluxionRHIPipelineHandle pipeline);
FluxionRHID3D12Pipeline* Fluxion_RHID3D12_ResolvePipeline(FluxionRHIPipelineHandle pipeline);

bool Fluxion_RHID3D12_SavePipelineCacheToFile(FluxionRHIDeviceHandle device, const char* path);
bool Fluxion_RHID3D12_LoadPipelineCacheFromFile(FluxionRHIDeviceHandle device, const char* path);

// --- Command lists / format / barrier mapping (D3D12CommandList.cpp) -------

FluxionRHICommandListHandle Fluxion_RHID3D12_CreateCommandList(FluxionRHIDeviceHandle device, FluxionRHIQueueType type);
void Fluxion_RHID3D12_DestroyCommandList(FluxionRHICommandListHandle commandList);
ID3D12GraphicsCommandList* Fluxion_RHID3D12_ResolveCommandList(FluxionRHICommandListHandle commandList);

void Fluxion_RHID3D12_CommandListCopyBufferToTexture(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle src, usize srcOffset, FluxionRHITextureHandle dst, u32 mipLevel, u32 arrayLayer);
void Fluxion_RHID3D12_CommandListCopyTextureToBuffer(FluxionRHICommandListHandle commandList, FluxionRHITextureHandle src, u32 mipLevel, u32 arrayLayer, FluxionRHIBufferHandle dst, usize dstOffset);
bool Fluxion_RHID3D12_DeviceIsFormatSupported(FluxionRHIDeviceHandle device, FluxionRHIFormat format);

DXGI_FORMAT Fluxion_RHID3D12_MapFormat(FluxionRHIFormat format);
FluxionRHIFormat Fluxion_RHID3D12_MapFormatBack(DXGI_FORMAT format);

// A depth texture that is also sampled -- a shadow map is the first this
// engine has -- cannot be one format here. D3D12 refuses a shader
// resource view over a depth format and a depth view over a colour one,
// so the RESOURCE is created without a type and each view says how it is
// being read. The other two backends take the depth format for both and
// need none of this.
//
// Both return their argument unchanged for anything that is not depth,
// so a caller does not have to ask first.
inline DXGI_FORMAT Fluxion_RHID3D12_DepthAsTypeless(DXGI_FORMAT format)
{
    if (format == DXGI_FORMAT_D32_FLOAT) return DXGI_FORMAT_R32_TYPELESS;
    if (format == DXGI_FORMAT_D24_UNORM_S8_UINT) return DXGI_FORMAT_R24G8_TYPELESS;
    return format;
}

inline DXGI_FORMAT Fluxion_RHID3D12_DepthAsShaderRead(DXGI_FORMAT format)
{
    if (format == DXGI_FORMAT_D32_FLOAT) return DXGI_FORMAT_R32_FLOAT;
    if (format == DXGI_FORMAT_D24_UNORM_S8_UINT) return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    return format;
}
D3D12_RESOURCE_STATES Fluxion_RHID3D12_MapResourceState(FluxionRHIResourceState state);

// --- Sync (D3D12CommandList.cpp) --------------------------------------------

FluxionRHIFenceHandle Fluxion_RHID3D12_CreateFence(FluxionRHIDeviceHandle device, bool signaled);
void Fluxion_RHID3D12_DestroyFence(FluxionRHIFenceHandle fence);
bool Fluxion_RHID3D12_WaitForFence(FluxionRHIFenceHandle fence);
void Fluxion_RHID3D12_ResetFence(FluxionRHIFenceHandle fence);
// Called once by QueueSubmit: returns the ID3D12Fence + value the submit
// should signal for this fence (or nullptr if the handle is invalid).
ID3D12Fence* Fluxion_RHID3D12_FenceBeginSignal(FluxionRHIFenceHandle fence, u64* outValue);

FluxionRHISemaphoreHandle Fluxion_RHID3D12_CreateSemaphore(FluxionRHIDeviceHandle device);
void Fluxion_RHID3D12_DestroySemaphore(FluxionRHISemaphoreHandle semaphore);

FluxionRHIQueryPoolHandle Fluxion_RHID3D12_CreateQueryPool(FluxionRHIDeviceHandle device, u32 queryCount);
void Fluxion_RHID3D12_DestroyQueryPool(FluxionRHIQueryPoolHandle queryPool);

// --- Swapchain (D3D12CommandList.cpp) ---------------------------------------

FluxionRHISwapchainHandle Fluxion_RHID3D12_CreateSwapchain(FluxionRHIDeviceHandle device, FluxionWindowHandle window, const FluxionRHISwapchainDesc* desc);
void Fluxion_RHID3D12_DestroySwapchain(FluxionRHISwapchainHandle swapchain);
u32 Fluxion_RHID3D12_SwapchainAcquireNextImage(FluxionRHISwapchainHandle swapchain, FluxionRHISemaphoreHandle signalSemaphore);
FluxionRHITextureHandle Fluxion_RHID3D12_SwapchainGetTexture(FluxionRHISwapchainHandle swapchain, u32 imageIndex);
void Fluxion_RHID3D12_SwapchainPresent(FluxionRHISwapchainHandle swapchain, u32 imageIndex, FluxionRHISemaphoreHandle waitSemaphore);
void Fluxion_RHID3D12_SwapchainGetExtent(FluxionRHISwapchainHandle swapchain, u32* outWidth, u32* outHeight);
void Fluxion_RHID3D12_CommandListResetQueryPool(FluxionRHICommandListHandle commandList, FluxionRHIQueryPoolHandle queryPool, u32 firstQuery, u32 queryCount);
void Fluxion_RHID3D12_CommandListWriteTimestamp(FluxionRHICommandListHandle commandList, FluxionRHIQueryPoolHandle queryPool, u32 queryIndex);
bool Fluxion_RHID3D12_QueryPoolGetResults(FluxionRHIQueryPoolHandle queryPool, u32 firstQuery, u32 queryCount, u64* outTicks);
u64 Fluxion_RHID3D12_GetTimestampFrequency(FluxionRHIDeviceHandle device);

#define FLUXION_RHID3D12_CHECK(expr) do { HRESULT fluxionHr_ = (expr); FLUXION_ASSERT_MSG(SUCCEEDED(fluxionHr_), #expr " failed"); } while (0)
