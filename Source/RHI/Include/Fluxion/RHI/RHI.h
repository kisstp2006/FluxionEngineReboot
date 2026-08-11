#pragma once

#include <Fluxion/Application/Window/WindowHandle.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RHI/Adapter.h>
#include <Fluxion/RHI/Capabilities.h>
#include <Fluxion/RHI/Format.h>
#include <Fluxion/RHI/Handles.h>
#include <Fluxion/RHI/ResourceDesc.h>
#include <Fluxion/RHI/ResourceState.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Instance / adapter -----------------------------------------------------

typedef enum FluxionRHIBackendType
{
    FLUXION_RHI_BACKEND_NULL = 0,
    FLUXION_RHI_BACKEND_VULKAN,
} FluxionRHIBackendType;

typedef struct FluxionRHIInstanceDesc
{
    const char* applicationName;
    bool enableValidation; // developer-build validation, where the backend has one
} FluxionRHIInstanceDesc;

FluxionRHIInstanceHandle Fluxion_RHI_CreateInstance(FluxionRHIBackendType backend, const FluxionRHIInstanceDesc* desc);
void Fluxion_RHI_DestroyInstance(FluxionRHIInstanceHandle instance);

// Two-call enumeration idiom (matches vkEnumeratePhysicalDevices): call
// with outAdapters == NULL to get the total count, then again with a
// buffer of that size. Returns the number of adapters written to
// outAdapters (or the total count, if outAdapters is NULL).
u32 Fluxion_RHI_EnumerateAdapters(FluxionRHIInstanceHandle instance, FluxionRHIAdapterHandle* outAdapters, u32 maxAdapters);
bool Fluxion_RHI_GetAdapterInfo(FluxionRHIAdapterHandle adapter, FluxionRHIAdapterInfo* outInfo);

// --- Device / queue ----------------------------------------------------------

typedef struct FluxionRHIDeviceDesc
{
    // Device creation fails (returns an invalid handle) if the adapter
    // cannot provide every capability set here.
    FluxionRHICapabilityFlags requiredCapabilities;
} FluxionRHIDeviceDesc;

FluxionRHIDeviceHandle Fluxion_RHI_CreateDevice(FluxionRHIAdapterHandle adapter, const FluxionRHIDeviceDesc* desc);
void Fluxion_RHI_DestroyDevice(FluxionRHIDeviceHandle device);

typedef enum FluxionRHIQueueType
{
    FLUXION_RHI_QUEUE_TYPE_GRAPHICS = 0,
    FLUXION_RHI_QUEUE_TYPE_COMPUTE,
    FLUXION_RHI_QUEUE_TYPE_TRANSFER,
} FluxionRHIQueueType;

// A backend may alias queue types onto the same underlying hardware
// queue (e.g. OpenGL always does; a GPU with no dedicated transfer
// family often does too) -- callers must not assume three independent
// hardware queues exist. Fluxion_RHI_GetAdapterInfo's
// AsyncCompute/AsyncTransfer capability bits say whether a type is truly
// independent on this device.
FluxionRHIQueueHandle Fluxion_RHI_GetQueue(FluxionRHIDeviceHandle device, FluxionRHIQueueType type);

// --- Command list --------------------------------------------------------

FluxionRHICommandListHandle Fluxion_RHI_CreateCommandList(FluxionRHIDeviceHandle device, FluxionRHIQueueType type);
void Fluxion_RHI_DestroyCommandList(FluxionRHICommandListHandle commandList);

void Fluxion_RHI_CommandList_Begin(FluxionRHICommandListHandle commandList);
void Fluxion_RHI_CommandList_End(FluxionRHICommandListHandle commandList);

typedef struct FluxionRHIRenderingAttachment
{
    FluxionRHITextureViewHandle view;
    bool clear;
    f32 clearColor[4]; // or clear depth in [0], stencil in [1] for a depth attachment
} FluxionRHIRenderingAttachment;

typedef struct FluxionRHIRenderingDesc
{
    const FluxionRHIRenderingAttachment* colorAttachments;
    u32 colorAttachmentCount;
    const FluxionRHIRenderingAttachment* depthAttachment; // optional, may be NULL
    u32 width;
    u32 height;
} FluxionRHIRenderingDesc;

void Fluxion_RHI_CommandList_BeginRendering(FluxionRHICommandListHandle commandList, const FluxionRHIRenderingDesc* desc);
void Fluxion_RHI_CommandList_EndRendering(FluxionRHICommandListHandle commandList);

void Fluxion_RHI_CommandList_SetPipeline(FluxionRHICommandListHandle commandList, FluxionRHIPipelineHandle pipeline);
void Fluxion_RHI_CommandList_SetVertexBuffer(FluxionRHICommandListHandle commandList, u32 slot, FluxionRHIBufferHandle buffer, usize offset);
void Fluxion_RHI_CommandList_SetIndexBuffer(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle buffer, usize offset, bool use16BitIndices);

void Fluxion_RHI_CommandList_Draw(FluxionRHICommandListHandle commandList, u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance);
void Fluxion_RHI_CommandList_DrawIndexed(FluxionRHICommandListHandle commandList, u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance);
void Fluxion_RHI_CommandList_DrawIndirect(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle argsBuffer, usize offset, u32 drawCount, u32 stride);
void Fluxion_RHI_CommandList_Dispatch(FluxionRHICommandListHandle commandList, u32 groupCountX, u32 groupCountY, u32 groupCountZ);

void Fluxion_RHI_CommandList_CopyBuffer(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle src, usize srcOffset, FluxionRHIBufferHandle dst, usize dstOffset, usize size);
void Fluxion_RHI_CommandList_CopyTexture(FluxionRHICommandListHandle commandList, FluxionRHITextureHandle src, FluxionRHITextureHandle dst);

typedef struct FluxionRHIBarrier
{
    // Exactly one of these is valid (FLUXION_HANDLE_IS_VALID); the other
    // stays a default-initialized (invalid) handle.
    FluxionRHITextureHandle texture;
    FluxionRHIBufferHandle buffer;
    FluxionRHIResourceState before;
    FluxionRHIResourceState after;
} FluxionRHIBarrier;

void Fluxion_RHI_CommandList_Barrier(FluxionRHICommandListHandle commandList, const FluxionRHIBarrier* barriers, u32 barrierCount);

// --- Submission ------------------------------------------------------------

void Fluxion_RHI_Queue_Submit(FluxionRHIQueueHandle queue, const FluxionRHICommandListHandle* commandLists, u32 commandListCount, FluxionRHIFenceHandle signalFence);

// --- Resources ---------------------------------------------------------------

FluxionRHIBufferHandle Fluxion_RHI_CreateBuffer(FluxionRHIDeviceHandle device, const FluxionRHIBufferDesc* desc);
void Fluxion_RHI_DestroyBuffer(FluxionRHIBufferHandle buffer);

FluxionRHITextureHandle Fluxion_RHI_CreateTexture(FluxionRHIDeviceHandle device, const FluxionRHITextureDesc* desc);
void Fluxion_RHI_DestroyTexture(FluxionRHITextureHandle texture);

typedef struct FluxionRHITextureViewDesc
{
    FluxionRHITextureHandle texture;
    FluxionRHIFormat format;
    u32 baseMipLevel;
    u32 mipLevelCount;
    u32 baseArrayLayer;
    u32 arrayLayerCount;
} FluxionRHITextureViewDesc;

FluxionRHITextureViewHandle Fluxion_RHI_CreateTextureView(FluxionRHIDeviceHandle device, const FluxionRHITextureViewDesc* desc);
void Fluxion_RHI_DestroyTextureView(FluxionRHITextureViewHandle view);

FluxionRHISamplerHandle Fluxion_RHI_CreateSampler(FluxionRHIDeviceHandle device, const FluxionRHISamplerDesc* desc);
void Fluxion_RHI_DestroySampler(FluxionRHISamplerHandle sampler);

// --- Shaders / pipelines ------------------------------------------------------
//
// This is the shape the doc's Milestone 21 (binding model + pipeline cache +
// bindless) builds on top of -- Milestone 18/19 only need a minimal,
// hand-fed path (no reflection, no logical binding groups yet) to prove a
// single hardcoded triangle draws.

typedef enum FluxionRHIShaderStage
{
    FLUXION_RHI_SHADER_STAGE_VERTEX = 0,
    FLUXION_RHI_SHADER_STAGE_FRAGMENT,
    FLUXION_RHI_SHADER_STAGE_COMPUTE,
} FluxionRHIShaderStage;

typedef struct FluxionRHIShaderDesc
{
    FluxionRHIShaderStage stage;
    const void* bytecode;   // backend-specific bytecode (e.g. SPIR-V for Vulkan)
    usize bytecodeSize;
    const char* entryPoint;
    const char* debugName;  // optional, may be NULL
} FluxionRHIShaderDesc;

FluxionRHIShaderHandle Fluxion_RHI_CreateShader(FluxionRHIDeviceHandle device, const FluxionRHIShaderDesc* desc);
void Fluxion_RHI_DestroyShader(FluxionRHIShaderHandle shader);

typedef enum FluxionRHIPrimitiveTopology
{
    FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST = 0,
    FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
    FLUXION_RHI_PRIMITIVE_TOPOLOGY_LINE_LIST,
    FLUXION_RHI_PRIMITIVE_TOPOLOGY_POINT_LIST,
} FluxionRHIPrimitiveTopology;

typedef enum FluxionRHICullMode
{
    FLUXION_RHI_CULL_MODE_NONE = 0,
    FLUXION_RHI_CULL_MODE_FRONT,
    FLUXION_RHI_CULL_MODE_BACK,
} FluxionRHICullMode;

typedef struct FluxionRHIRasterState
{
    FluxionRHICullMode cullMode;
    bool frontFaceCounterClockwise;
    bool wireframe;
} FluxionRHIRasterState;

typedef enum FluxionRHICompareOp
{
    FLUXION_RHI_COMPARE_OP_NEVER = 0,
    FLUXION_RHI_COMPARE_OP_LESS,
    FLUXION_RHI_COMPARE_OP_EQUAL,
    FLUXION_RHI_COMPARE_OP_LESS_OR_EQUAL,
    FLUXION_RHI_COMPARE_OP_GREATER,
    FLUXION_RHI_COMPARE_OP_NOT_EQUAL,
    FLUXION_RHI_COMPARE_OP_GREATER_OR_EQUAL,
    FLUXION_RHI_COMPARE_OP_ALWAYS,
} FluxionRHICompareOp;

typedef struct FluxionRHIDepthState
{
    bool testEnable;
    bool writeEnable;
    FluxionRHICompareOp compareOp;
} FluxionRHIDepthState;

typedef struct FluxionRHIBlendState
{
    bool blendEnable;
    // FLUXION_RHI_BLEND_STATE_STANDARD_ALPHA below covers the common
    // "src alpha, one minus src alpha" case; a fuller per-factor/per-op
    // blend descriptor is Milestone 21's job once a real material system
    // needs more than that.
} FluxionRHIBlendState;

#define FLUXION_RHI_MAX_VERTEX_ATTRIBUTES 16
#define FLUXION_RHI_MAX_RENDER_TARGETS 8

typedef struct FluxionRHIVertexAttribute
{
    u32 location;
    FluxionRHIFormat format;
    u32 offset;
} FluxionRHIVertexAttribute;

typedef struct FluxionRHIVertexLayout
{
    FluxionRHIVertexAttribute attributes[FLUXION_RHI_MAX_VERTEX_ATTRIBUTES];
    u32 attributeCount;
    u32 stride; // single interleaved vertex buffer binding, for now
} FluxionRHIVertexLayout;

typedef struct FluxionRHIGraphicsPipelineDesc
{
    FluxionRHIShaderHandle vertexShader;
    FluxionRHIShaderHandle fragmentShader;
    FluxionRHIVertexLayout vertexLayout;
    FluxionRHIRasterState rasterState;
    FluxionRHIDepthState depthState;
    FluxionRHIBlendState blendState;
    FluxionRHIPrimitiveTopology topology;
    FluxionRHIFormat colorFormats[FLUXION_RHI_MAX_RENDER_TARGETS];
    u32 colorFormatCount;
    FluxionRHIFormat depthFormat; // FLUXION_RHI_FORMAT_UNKNOWN if no depth attachment
    const char* debugName; // optional, may be NULL
} FluxionRHIGraphicsPipelineDesc;

FluxionRHIPipelineHandle Fluxion_RHI_CreateGraphicsPipeline(FluxionRHIDeviceHandle device, const FluxionRHIGraphicsPipelineDesc* desc);
void Fluxion_RHI_DestroyPipeline(FluxionRHIPipelineHandle pipeline);

// --- Swapchain -----------------------------------------------------------

typedef struct FluxionRHISwapchainDesc
{
    u32 width;
    u32 height;
    FluxionRHIFormat format;
    u32 bufferCount; // frames-in-flight, e.g. 2 or 3
    bool vsync;
} FluxionRHISwapchainDesc;

FluxionRHISwapchainHandle Fluxion_RHI_CreateSwapchain(FluxionRHIDeviceHandle device, FluxionWindowHandle window, const FluxionRHISwapchainDesc* desc);
void Fluxion_RHI_DestroySwapchain(FluxionRHISwapchainHandle swapchain);

u32 Fluxion_RHI_Swapchain_AcquireNextImage(FluxionRHISwapchainHandle swapchain, FluxionRHISemaphoreHandle signalSemaphore);
FluxionRHITextureHandle Fluxion_RHI_Swapchain_GetTexture(FluxionRHISwapchainHandle swapchain, u32 imageIndex);
void Fluxion_RHI_Swapchain_Present(FluxionRHISwapchainHandle swapchain, u32 imageIndex, FluxionRHISemaphoreHandle waitSemaphore);

// --- Synchronization ----------------------------------------------------------

FluxionRHIFenceHandle Fluxion_RHI_CreateFence(FluxionRHIDeviceHandle device, bool signaled);
void Fluxion_RHI_DestroyFence(FluxionRHIFenceHandle fence);
void Fluxion_RHI_WaitForFence(FluxionRHIFenceHandle fence);
void Fluxion_RHI_ResetFence(FluxionRHIFenceHandle fence);

FluxionRHISemaphoreHandle Fluxion_RHI_CreateSemaphore(FluxionRHIDeviceHandle device);
void Fluxion_RHI_DestroySemaphore(FluxionRHISemaphoreHandle semaphore);

FluxionRHIQueryPoolHandle Fluxion_RHI_CreateQueryPool(FluxionRHIDeviceHandle device, u32 queryCount);
void Fluxion_RHI_DestroyQueryPool(FluxionRHIQueryPoolHandle queryPool);

#ifdef __cplusplus
}
#endif
