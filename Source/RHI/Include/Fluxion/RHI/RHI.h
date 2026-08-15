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
    FLUXION_RHI_BACKEND_OPENGL,
    FLUXION_RHI_BACKEND_D3D12,
} FluxionRHIBackendType;

// Whether this build has the named backend in it at all.
//
// Which backends exist is settled when the engine is built, not when it
// runs: D3D12 is a Windows API, and the OpenGL backend here is desktop GL
// reached through WGL or GLX, so neither is compiled for a target that
// has no such thing. Asking for one that was left out is refused, and a
// caller that means to fall back to another needs to know before it asks
// rather than after.
//
// True is not a promise that creating the instance will succeed -- the
// driver may still be missing or too old. It is the narrower statement
// that this build knows how to try.
bool Fluxion_RHI_IsBackendAvailable(FluxionRHIBackendType backend);

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

// A backend may not be able to free a resource's GPU memory the instant
// Destroy* is called -- the GPU could still be reading it from a
// command buffer submitted earlier. Destroy* only marks a resource for
// retirement; the caller must call this once per frame (e.g. alongside
// Swapchain_Present) so the backend can reclaim anything whose GPU
// timeline has actually completed. A backend with nothing GPU-timeline-
// bound to defer against (e.g. the Null backend) may treat this as a
// no-op.
void Fluxion_RHI_Device_CollectGarbage(FluxionRHIDeviceHandle device);

// Returns which backend the device belongs to. Useful for callers that
// need to branch shader-compilation or resource-format choices per
// backend (e.g. ShaderProgram compiling to GLSL/DXIL/SPIR-V).
FluxionRHIBackendType Fluxion_RHI_GetDeviceBackendType(FluxionRHIDeviceHandle device);

// Whether this device can hold a sampled texture in this format at all.
//
// Asked BEFORE creating one rather than after. A backend told to create a
// texture in a format it does not have does not simply hand back an
// invalid handle: it reports an error, and a build with validation
// switched on treats a reported error as a fault and stops. So "can you"
// and "please do" have to be two different questions, and this is the
// first one.
//
// Which formats exist is not a property of the backend but of the
// hardware behind it: no desktop GPU reads ASTC, most mobile ones do not
// read BC, and a cook target picks between them on exactly this answer.
bool Fluxion_RHI_Device_IsFormatSupported(FluxionRHIDeviceHandle device, FluxionRHIFormat format);

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
// The layout CopyBufferToTexture reads pixel data in. Every row must
// start FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT bytes apart, and every
// mip's data must start at a srcOffset that is a multiple of
// FLUXION_RHI_TEXTURE_DATA_PLACEMENT_ALIGNMENT.
//
// A "row" here is a row of BLOCKS, which is a row of texels only for the
// formats whose block covers one texel. Fluxion_RHI_GetFormatRowBytes
// (Format.h) answers how wide one is; asking it rather than multiplying
// by a bytes-per-texel is what keeps a compressed texture from being
// staged as though it were eight times its real size -- an error that
// makes the buffer too big rather than too small, so nothing fails and
// the picture is simply wrong. These are the strictest
// backend's rules, adopted for all of them: a layout defined per backend
// would mean staging data laid out for one is quietly misread by
// another, and "quietly" is the operative word -- a 256-byte-wide image
// happens to satisfy both tight packing and this rule at once, which is
// exactly how such a disagreement stays invisible until the first
// narrower mip level.
#define FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT       ((usize)256)
#define FLUXION_RHI_TEXTURE_DATA_PLACEMENT_ALIGNMENT ((usize)512)

// The only CPU->GPU texture upload path this contract offers: a caller
// stages pixel data into a CPU_TO_GPU/GPU_ONLY-transfer-source buffer
// (Map/write/Unmap, same as any other staging upload) and copies it into
// mip level `mipLevel`/array layer `arrayLayer` of `dst` here, at full
// texture extent (no sub-region offset/size -- add one only once
// something actually needs partial uploads). Rows in the buffer must be
// laid out to FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT and srcOffset to
// FLUXION_RHI_TEXTURE_DATA_PLACEMENT_ALIGNMENT -- see above. `dst` must
// already be in FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION via a prior
// Barrier call, same as CopyTexture's own destination requirement.
void Fluxion_RHI_CommandList_CopyBufferToTexture(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle src, usize srcOffset, FluxionRHITextureHandle dst, u32 mipLevel, u32 arrayLayer);

// The same journey the other way: one mip level of one array layer of
// `src` into `dst` at `dstOffset`, in exactly the layout described above
// -- rows FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT apart, dstOffset a
// multiple of FLUXION_RHI_TEXTURE_DATA_PLACEMENT_ALIGNMENT. `src` must
// already be in FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE via a prior
// Barrier call, and `dst` wants a memory class the CPU can read
// (GPU_TO_CPU/READBACK) if anything is going to look at it.
//
// What this is for: asking the GPU what it actually made of something.
// A compressed texture is decoded by fixed-function hardware, and the
// only way to find out whether an encoder and that hardware agree is to
// sample it and read the result back -- an encoder checked against
// nothing but its own matching decoder agrees with itself and proves
// nothing.
void Fluxion_RHI_CommandList_CopyTextureToBuffer(FluxionRHICommandListHandle commandList, FluxionRHITextureHandle src, u32 mipLevel, u32 arrayLayer, FluxionRHIBufferHandle dst, usize dstOffset);

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

// Only valid for a buffer created with a CPU-visible FluxionRHIMemoryClass
// (CPU_TO_GPU/GPU_TO_CPU/READBACK) -- returns NULL for a GPU_ONLY/
// TRANSIENT buffer. This is the only portable way to get CPU data into a
// buffer the RHI contract offers (no raw-pointer "initial data" field on
// FluxionRHIBufferDesc, deliberately, since the caller may want to write
// into a persistently-mapped ring/staging buffer many times); the copy
// onto a GPU_ONLY destination still goes through
// Fluxion_RHI_CommandList_CopyBuffer as usual.
void* Fluxion_RHI_MapBuffer(FluxionRHIBufferHandle buffer);
void Fluxion_RHI_UnmapBuffer(FluxionRHIBufferHandle buffer);

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

    // How the shader reads it, which need not be how it was made: a cube
    // texture viewed as CUBE is sampled by direction, and the same
    // texture viewed as 2D is six ordinary layers -- which is what
    // rendering into one face needs.
    //
    // At the end, and zero meaning 2D, so every positional initializer
    // written before this existed still says what it said.
    FluxionRHITextureDimension dimension;
} FluxionRHITextureViewDesc;

FluxionRHITextureViewHandle Fluxion_RHI_CreateTextureView(FluxionRHIDeviceHandle device, const FluxionRHITextureViewDesc* desc);
void Fluxion_RHI_DestroyTextureView(FluxionRHITextureViewHandle view);

FluxionRHISamplerHandle Fluxion_RHI_CreateSampler(FluxionRHIDeviceHandle device, const FluxionRHISamplerDesc* desc);
void Fluxion_RHI_DestroySampler(FluxionRHISamplerHandle sampler);

// A bitmask over FluxionRHIShaderStage values (declared below), so a
// single binding can be marked visible to more than one stage (e.g. a
// Frame-group constant buffer read by both the vertex and fragment
// shaders) without a separate entry per stage.
typedef u32 FluxionRHIShaderStageFlags;
#define FLUXION_RHI_SHADER_STAGE_FLAG_VERTEX   ((FluxionRHIShaderStageFlags)(1u << 0))
#define FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT ((FluxionRHIShaderStageFlags)(1u << 1))
#define FLUXION_RHI_SHADER_STAGE_FLAG_COMPUTE  ((FluxionRHIShaderStageFlags)(1u << 2))

// --- Binding model -----------------------------------------------------------
//
// A BindGroupLayout describes the shape of one binding-frequency group
// (its list of bindings and their types/stages); a BindGroup is a
// concrete set of resources bound to match that shape. Every pipeline
// takes up to FLUXION_RHI_MAX_BIND_GROUPS layouts, one per logical
// binding frequency -- Global (per-instance-wide constants), Frame
// (per-frame constants, e.g. view/projection), Material (per-material
// textures/constants), and Object (per-draw constants). A backend is
// free to alias/cache the underlying native object across
// layouts/groups that describe the same shape.

#define FLUXION_RHI_MAX_BIND_GROUPS 4
#define FLUXION_RHI_BIND_GROUP_GLOBAL 0
#define FLUXION_RHI_BIND_GROUP_FRAME 1
#define FLUXION_RHI_BIND_GROUP_MATERIAL 2
#define FLUXION_RHI_BIND_GROUP_OBJECT 3

#define FLUXION_RHI_MAX_BIND_GROUP_ENTRIES 16

typedef enum FluxionRHIBindingType
{
    FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER = 0,
    FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER,
    FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE,
    FLUXION_RHI_BINDING_TYPE_SAMPLER,
} FluxionRHIBindingType;

typedef struct FluxionRHIBindGroupLayoutEntryDesc
{
    u32 binding;
    FluxionRHIBindingType type;
    FluxionRHIShaderStageFlags visibility;
    // Only meaningful for FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE: 0
    // means a single, fixed-size binding; >0 requests an array of that
    // many descriptors at this binding, sized dynamically per FluxionRHIBindGroup
    // (only valid together with FluxionRHIBindGroupLayoutDesc::bindless).
    u32 arrayCount;
} FluxionRHIBindGroupLayoutEntryDesc;

typedef struct FluxionRHIBindGroupLayoutDesc
{
    FluxionRHIBindGroupLayoutEntryDesc entries[FLUXION_RHI_MAX_BIND_GROUP_ENTRIES];
    u32 entryCount;
    // Requests a variable-count, update-after-bind layout for its array
    // entries (FLUXION_RHI_CAPABILITY_DESCRIPTOR_INDEXING required) --
    // a backend without that capability creates an ordinary, fixed-size
    // layout instead (the "bindful fallback"), so callers must not
    // assume this flag was honored; check FluxionRHIBindGroupHandle's
    // validity as usual and size arrayCount conservatively either way.
    bool bindless;
    const char* debugName; // optional, may be NULL
} FluxionRHIBindGroupLayoutDesc;

// Identical binding-layout descriptions may return the same underlying
// native layout object (a backend is free to cache/dedupe by shape) --
// callers must not rely on two calls with equal descs returning distinct
// handles.
FluxionRHIBindGroupLayoutHandle Fluxion_RHI_CreateBindGroupLayout(FluxionRHIDeviceHandle device, const FluxionRHIBindGroupLayoutDesc* desc);
void Fluxion_RHI_DestroyBindGroupLayout(FluxionRHIBindGroupLayoutHandle layout);

typedef struct FluxionRHIBindGroupEntry
{
    u32 binding;
    FluxionRHIBindingType type;
    // Exactly one of these is valid, matching `type`.
    FluxionRHIBufferHandle buffer;
    usize bufferOffset;
    usize bufferSize;
    FluxionRHITextureViewHandle textureView;
    FluxionRHISamplerHandle sampler;

    // How many bytes one element of a STORAGE_BUFFER is. Ignored by every
    // other binding type.
    //
    // This is not a backend detail leaking into the portable entry, even
    // though only one backend reads it: the size of an element is part of
    // what the SHADER says the buffer is, and the caller that filled the
    // buffer is the one that knows. A backend which describes a buffer
    // view by element rather than by byte has no other way to find out,
    // and guessing produces a view that reads the right memory in the
    // wrong pieces -- which is not an error anywhere, just wrong numbers.
    //
    // Zero means four bytes, which is what a buffer of plain floats or
    // integers wants and what this contract assumed before it could be
    // said out loud.
    u32 bufferElementStride;
} FluxionRHIBindGroupEntry;

typedef struct FluxionRHIBindGroupDesc
{
    FluxionRHIBindGroupLayoutHandle layout;
    const FluxionRHIBindGroupEntry* entries;
    u32 entryCount;
} FluxionRHIBindGroupDesc;

FluxionRHIBindGroupHandle Fluxion_RHI_CreateBindGroup(FluxionRHIDeviceHandle device, const FluxionRHIBindGroupDesc* desc);
// Deferred destruction, same contract as every other Destroy* here --
// safe to call while the GPU may still be reading the group; actually
// reclaimed on a later Fluxion_RHI_Device_CollectGarbage.
void Fluxion_RHI_DestroyBindGroup(FluxionRHIBindGroupHandle bindGroup);

void Fluxion_RHI_CommandList_SetBindGroup(FluxionRHICommandListHandle commandList, u32 groupIndex, FluxionRHIBindGroupHandle bindGroup);

// --- Shaders / pipelines ------------------------------------------------------

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
    // When enabled, a backend applies the common "src alpha, one minus
    // src alpha" blend factors -- a fuller per-factor/per-op blend
    // descriptor is only worth adding once something actually needs more
    // control than that.
    bool blendEnable;
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
    // Index i corresponds to FLUXION_RHI_BIND_GROUP_* i; an invalid
    // (default-initialized) handle at an index means that group is
    // unused by this pipeline. bindGroupLayoutCount is the number of
    // leading entries in bindGroupLayouts that are meaningful (trailing
    // ones are ignored), not a count of valid handles within it.
    FluxionRHIBindGroupLayoutHandle bindGroupLayouts[FLUXION_RHI_MAX_BIND_GROUPS];
    u32 bindGroupLayoutCount;
    const char* debugName; // optional, may be NULL
} FluxionRHIGraphicsPipelineDesc;

FluxionRHIPipelineHandle Fluxion_RHI_CreateGraphicsPipeline(FluxionRHIDeviceHandle device, const FluxionRHIGraphicsPipelineDesc* desc);
void Fluxion_RHI_DestroyPipeline(FluxionRHIPipelineHandle pipeline);

typedef struct FluxionRHIComputePipelineDesc
{
    FluxionRHIShaderHandle computeShader;
    FluxionRHIBindGroupLayoutHandle bindGroupLayouts[FLUXION_RHI_MAX_BIND_GROUPS];
    u32 bindGroupLayoutCount;
    const char* debugName; // optional, may be NULL
} FluxionRHIComputePipelineDesc;

// Returns a FluxionRHIPipelineHandle, the same handle type as a graphics
// pipeline -- Fluxion_RHI_CommandList_SetPipeline/DestroyPipeline work
// on either kind unchanged; only creation is stage-specific.
FluxionRHIPipelineHandle Fluxion_RHI_CreateComputePipeline(FluxionRHIDeviceHandle device, const FluxionRHIComputePipelineDesc* desc);

// --- Pipeline cache ------------------------------------------------------------
//
// One cache per device, populated implicitly by every
// Fluxion_RHI_Create{Graphics,Compute}Pipeline call. Persistence is
// entirely caller-driven -- the backend never reads or writes a file on
// its own; a caller loads a previously-saved cache right after device
// creation (before creating any pipelines) to benefit from it, and saves
// it whenever convenient (e.g. on shutdown).

// Returns false if the device has no pipeline cache support or the file
// could not be written; never treated as a hard failure by the backend.
bool Fluxion_RHI_Device_SavePipelineCacheToFile(FluxionRHIDeviceHandle device, const char* path);
// Returns false if the file is missing, unreadable, or was produced by
// an incompatible driver/backend version -- the device's cache is left
// empty (not partially populated) in that case, exactly as if this had
// never been called.
bool Fluxion_RHI_Device_LoadPipelineCacheFromFile(FluxionRHIDeviceHandle device, const char* path);

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

// The swapchain's actual current image extent -- can differ from the
// width/height originally passed to Fluxion_RHI_CreateSwapchain (window
// resize, DPI adjustments, a resize the backend already absorbed on a
// prior Acquire/Present). A caller building a FluxionRHIRenderingDesc
// around a swapchain-acquired texture must use this, not a separately
// queried window size, or risk a size mismatch between the two.
//
// Query it *after* acquiring, never before: a backend is free to notice a
// resize during the acquire and rebuild there, so an extent read
// beforehand describes the swapchain that is being replaced.
//
// Zero means there is currently no drawable surface at all -- a minimised
// window, most commonly. The acquire that preceded it produced nothing
// usable, so the whole frame has to be skipped: there is nothing to
// render into, and nothing to present either.
void Fluxion_RHI_Swapchain_GetExtent(FluxionRHISwapchainHandle swapchain, u32* outWidth, u32* outHeight);

// --- Synchronization ----------------------------------------------------------

FluxionRHIFenceHandle Fluxion_RHI_CreateFence(FluxionRHIDeviceHandle device, bool signaled);
void Fluxion_RHI_DestroyFence(FluxionRHIFenceHandle fence);
// Returns false if the wait timed out (a bounded internal timeout, not a
// caller-supplied one) instead of observing the fence actually signal --
// the caller cannot then assume the underlying GPU submission is done, and
// should not touch/destroy any resource that submission may still be using.
bool Fluxion_RHI_WaitForFence(FluxionRHIFenceHandle fence);
void Fluxion_RHI_ResetFence(FluxionRHIFenceHandle fence);

FluxionRHISemaphoreHandle Fluxion_RHI_CreateSemaphore(FluxionRHIDeviceHandle device);
void Fluxion_RHI_DestroySemaphore(FluxionRHISemaphoreHandle semaphore);

FluxionRHIQueryPoolHandle Fluxion_RHI_CreateQueryPool(FluxionRHIDeviceHandle device, u32 queryCount);
void Fluxion_RHI_DestroyQueryPool(FluxionRHIQueryPoolHandle queryPool);

// --- GPU timestamps -------------------------------------------------------
//
// The pool holds timestamp queries only. The intended shape of a frame:
// reset the queries about to be reused, write one at each point of
// interest, submit, wait the frame's fence, then read the results back
// and convert ticks to time with the device's timestamp frequency.

// Makes [firstQuery, firstQuery+queryCount) writable again. Recorded on a
// command list rather than done host-side, because that is the one shape
// every backend can express: a backend whose queries need no reset simply
// records nothing. A query must be reset on the same or an earlier
// submission than the write that reuses it -- including its very first
// write.
void Fluxion_RHI_CommandList_ResetQueryPool(FluxionRHICommandListHandle commandList, FluxionRHIQueryPoolHandle queryPool, u32 firstQuery, u32 queryCount);

// Records "write the GPU clock into query `queryIndex` once all work
// recorded before this point has finished".
void Fluxion_RHI_CommandList_WriteTimestamp(FluxionRHICommandListHandle commandList, FluxionRHIQueryPoolHandle queryPool, u32 queryIndex);

// Copies [firstQuery, firstQuery+queryCount) into outTicks, in GPU clock
// ticks (divide by the device's timestamp frequency for seconds).
// Returns false if the range is invalid or a backend can tell the values
// are not ready yet. Not every backend can tell: results are only
// defined once the submissions that wrote them have completed, which the
// caller establishes by waiting the same fence it already waits per
// frame -- reading earlier is a caller bug this function is allowed, but
// not required, to catch.
bool Fluxion_RHI_QueryPool_GetResults(FluxionRHIQueryPoolHandle queryPool, u32 firstQuery, u32 queryCount, u64* outTicks);

// Ticks per second of the clock WriteTimestamp samples. Constant for the
// life of the device; zero only if the device is invalid.
u64 Fluxion_RHI_Device_GetTimestampFrequency(FluxionRHIDeviceHandle device);

// --- Validation -------------------------------------------------------------
//
// Alive only when the instance was created with enableValidation: an
// engine-owned checking layer wraps the backend and reports (logs and
// counts) invalid calls instead of asserting, so a test can drive
// deliberately broken calls into it and assert on this count -- and a
// developer sees every mistake in a frame rather than only the first.
// With validation off, the count simply stays zero.
u64 Fluxion_RHI_Validation_GetErrorCount(void);
void Fluxion_RHI_Validation_ResetErrorCount(void);

#ifdef __cplusplus
}
#endif
