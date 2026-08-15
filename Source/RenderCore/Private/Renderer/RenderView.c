#include <Fluxion/RenderCore/Renderer/RenderView.h>

#include "RendererInternal.h"

#include <Fluxion/Foundation/Assert.h>

#include <string.h>

typedef struct FluxionRenderViewRecord
{
    bool alive;
    u32 generation;

    FluxionRHIDeviceHandle device;

    FluxionMat4 viewMatrix;
    FluxionMat4 projectionMatrix;
    FluxionViewport viewport;
    FluxionScissorRect scissor;
    FluxionRenderTargetHandle renderTarget;
    u32 layerMask;

    FluxionVec3 ambientColor;
    f32 exposure;
    f32 tonemapWhitePoint;
    bool encodeOutputToSRGB;

    // The lights, in the packed shape the shader reads, plus the two
    // buffers that get them there: one the CPU writes, one the GPU reads.
    FluxionRHIBufferHandle lightStaging;
    FluxionRHIBufferHandle lightStorage;
    u32 lightCapacity; // in lights, never shrinks
    u32 lightCount;

    FluxionRHIBufferHandle frameConstantBuffer; // uniform buffer holding this frame's FluxionFrameConstants
    FluxionRHIBindGroupLayoutHandle frameBindGroupLayout;
    FluxionRHIBindGroupHandle frameBindGroup;
} FluxionRenderViewRecord;

// What one light looks like to a shader.
//
// Four vectors of four, and every field packed into them by hand, because
// the two shading languages this compiles to do not lay out a structure
// the same way when it mixes three-component vectors with single floats.
// Four-component vectors are the one shape both agree on without a rule
// anybody has to remember -- and a layout the two ends disagreed about
// would not fail, it would read the right memory in the wrong pieces.
//
// The spare components are not waste worth removing: sixty-four bytes a
// light is nothing beside being sure the two sides agree.
typedef struct FluxionRenderLightGPU
{
    f32 positionRange[4]; // xyz world position, w range
    f32 direction[4];     // xyz the way the light travels, w which kind it is
    f32 color[4];         // rgb colour-and-intensity, w cos(inner cone)
    f32 cone[4];          // x cos(outer cone), yzw spare
} FluxionRenderLightGPU;

// How many lights a view starts with room for. Not a limit -- the storage
// grows -- only the point at which growing starts.
#define FLUXION_RENDER_VIEW_INITIAL_LIGHTS 8

static FluxionRenderViewRecord s_renderViews[FLUXION_RENDERER_MAX_RENDER_VIEWS];

// Both buffers and the bind group together: the group names the buffer,
// so a buffer replaced without the group is a group pointing at something
// that no longer exists.
static bool Fluxion_RenderViewInternal_MakeLightStorage(FluxionRHIDeviceHandle device, u32 capacity,
                                                        FluxionRHIBufferHandle* outStaging, FluxionRHIBufferHandle* outStorage)
{
    const usize bytes = (usize)capacity * sizeof(FluxionRenderLightGPU);

    FluxionRHIBufferDesc stagingDesc;
    stagingDesc.size = bytes;
    stagingDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC;
    stagingDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    stagingDesc.debugName = "Fluxion.RenderView.LightStaging";
    *outStaging = Fluxion_RHI_CreateBuffer(device, &stagingDesc);
    if (!FLUXION_HANDLE_IS_VALID(*outStaging)) return false;

    // GPU-only, and that is forced rather than chosen: a buffer the CPU
    // can write lives in memory that at least one backend refuses to give
    // a structured view of. So the CPU writes one buffer, the GPU reads
    // another, and a recorded copy joins them.
    FluxionRHIBufferDesc storageDesc;
    storageDesc.size = bytes;
    storageDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_STORAGE_BUFFER | FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST;
    storageDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    storageDesc.debugName = "Fluxion.RenderView.Lights";
    *outStorage = Fluxion_RHI_CreateBuffer(device, &storageDesc);
    if (!FLUXION_HANDLE_IS_VALID(*outStorage))
    {
        Fluxion_RHI_DestroyBuffer(*outStaging);
        return false;
    }

    return true;
}

static FluxionRHIBindGroupHandle Fluxion_RenderViewInternal_MakeFrameBindGroup(FluxionRHIDeviceHandle device,
                                                                               FluxionRHIBindGroupLayoutHandle layout,
                                                                               FluxionRHIBufferHandle constants,
                                                                               FluxionRHIBufferHandle lights, u32 lightCapacity)
{
    FluxionRHIBindGroupEntry entries[2];
    memset(entries, 0, sizeof(entries));

    entries[0].binding = 0;
    entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    entries[0].buffer = constants;
    entries[0].bufferSize = sizeof(FluxionFrameConstants);
    entries[0].textureView = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    entries[0].sampler = (FluxionRHISamplerHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    entries[1].binding = 1;
    entries[1].type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    entries[1].buffer = lights;
    entries[1].bufferSize = (usize)lightCapacity * sizeof(FluxionRenderLightGPU);
    entries[1].textureView = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    entries[1].sampler = (FluxionRHISamplerHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    // How big one light is, said rather than assumed. A backend that
    // describes a buffer by element cannot work it out, and a wrong guess
    // reads the right memory in the wrong pieces.
    entries[1].bufferElementStride = (u32)sizeof(FluxionRenderLightGPU);

    FluxionRHIBindGroupDesc desc;
    desc.layout = layout;
    desc.entries = entries;
    desc.entryCount = 2;
    return Fluxion_RHI_CreateBindGroup(device, &desc);
}

static FluxionRenderViewRecord* Fluxion_RenderViewInternal_Resolve(FluxionRenderViewHandle handle)
{
    if (handle.index >= FLUXION_RENDERER_MAX_RENDER_VIEWS) return NULL;
    FluxionRenderViewRecord* record = &s_renderViews[handle.index];
    if (!record->alive || record->generation != handle.generation) return NULL;
    return record;
}

FluxionRenderViewHandle Fluxion_RenderView_Create(FluxionRHIDeviceHandle device, const FluxionRenderViewDesc* desc)
{
    FluxionRenderViewHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(desc != NULL);
    if (desc == NULL) return invalid;

    u32 index = FLUXION_RENDERER_MAX_RENDER_VIEWS;
    for (u32 i = 0; i < FLUXION_RENDERER_MAX_RENDER_VIEWS; ++i)
    {
        if (!s_renderViews[i].alive) { index = i; break; }
    }
    if (index == FLUXION_RENDERER_MAX_RENDER_VIEWS) return invalid;

    FluxionRHIBufferDesc bufferDesc;
    bufferDesc.size = sizeof(FluxionFrameConstants);
    bufferDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER;
    bufferDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    bufferDesc.debugName = "Fluxion.RenderView.FrameConstantBuffer";
    FluxionRHIBufferHandle frameConstantBuffer = Fluxion_RHI_CreateBuffer(device, &bufferDesc);

    FluxionRHIBindGroupLayoutDesc layoutDesc = FluxionRendererInternal_MakeFrameLayoutDesc();
    FluxionRHIBindGroupLayoutHandle frameBindGroupLayout = Fluxion_RHI_CreateBindGroupLayout(device, &layoutDesc);

    // Made now rather than when the first light arrives: the bind group
    // names this buffer, and a group with a hole in it is not a group a
    // backend will accept. A view with no lights binds an empty list,
    // which is a real answer.
    FluxionRHIBufferHandle lightStaging;
    FluxionRHIBufferHandle lightStorage;
    if (!Fluxion_RenderViewInternal_MakeLightStorage(device, FLUXION_RENDER_VIEW_INITIAL_LIGHTS, &lightStaging, &lightStorage))
    {
        Fluxion_RHI_DestroyBindGroupLayout(frameBindGroupLayout);
        Fluxion_RHI_DestroyBuffer(frameConstantBuffer);
        return invalid;
    }

    FluxionRHIBindGroupHandle frameBindGroup = Fluxion_RenderViewInternal_MakeFrameBindGroup(
        device, frameBindGroupLayout, frameConstantBuffer, lightStorage, FLUXION_RENDER_VIEW_INITIAL_LIGHTS);

    FluxionRenderViewRecord* record = &s_renderViews[index];
    u32 generation = record->generation;
    memset(record, 0, sizeof(*record));
    record->alive = true;
    record->generation = generation;
    record->device = device;
    record->viewMatrix = desc->viewMatrix;
    record->projectionMatrix = desc->projectionMatrix;
    record->viewport = desc->viewport;
    record->scissor = desc->scissor;
    record->renderTarget = desc->renderTarget;
    record->layerMask = desc->layerMask;
    record->ambientColor = desc->ambientColor;

    // Taken as one when nobody said. See the field's own comment: an
    // unset multiplier producing a black screen would read as a broken
    // renderer rather than as a description with a hole in it.
    record->exposure = desc->exposure > 0.0f ? desc->exposure : 1.0f;
    record->tonemapWhitePoint = desc->tonemapWhitePoint;
    record->encodeOutputToSRGB = desc->encodeOutputToSRGB;

    record->frameConstantBuffer = frameConstantBuffer;
    record->frameBindGroupLayout = frameBindGroupLayout;
    record->frameBindGroup = frameBindGroup;
    record->lightStaging = lightStaging;
    record->lightStorage = lightStorage;
    record->lightCapacity = FLUXION_RENDER_VIEW_INITIAL_LIGHTS;
    record->lightCount = 0;

    FluxionRenderViewHandle handle = { index, generation };
    return handle;
}

void Fluxion_RenderView_Destroy(FluxionRenderViewHandle view)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL)
    {
        FLUXION_ASSERT_MSG(false, "Fluxion_RenderView_Destroy called with an invalid or already-destroyed handle");
        return;
    }

    if (FLUXION_HANDLE_IS_VALID(record->lightStorage)) Fluxion_RHI_DestroyBuffer(record->lightStorage);
    if (FLUXION_HANDLE_IS_VALID(record->lightStaging)) Fluxion_RHI_DestroyBuffer(record->lightStaging);
    if (FLUXION_HANDLE_IS_VALID(record->frameBindGroup)) Fluxion_RHI_DestroyBindGroup(record->frameBindGroup);
    if (FLUXION_HANDLE_IS_VALID(record->frameBindGroupLayout)) Fluxion_RHI_DestroyBindGroupLayout(record->frameBindGroupLayout);
    if (FLUXION_HANDLE_IS_VALID(record->frameConstantBuffer)) Fluxion_RHI_DestroyBuffer(record->frameConstantBuffer);

    record->alive = false;
    ++record->generation;
}

void Fluxion_RenderView_UpdateFrameConstants(FluxionRenderViewHandle view)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return;

    FluxionFrameConstants constants;
    memset(&constants, 0, sizeof(constants));

    constants.viewProjection = Fluxion_Mat4_Multiply(record->projectionMatrix, record->viewMatrix);

    // Worked out from the view matrix rather than asked for separately.
    // A camera position given alongside a view matrix is a second place
    // to say where the camera is, and the two disagreeing produces
    // lighting that is wrong in a way nothing reports -- highlights in
    // the wrong place, which reads as an artist's mistake.
    const FluxionMat4 cameraToWorld = Fluxion_Mat4_RigidInverse(record->viewMatrix);
    constants.cameraPosition.x = cameraToWorld.m[0][3];
    constants.cameraPosition.y = cameraToWorld.m[1][3];
    constants.cameraPosition.z = cameraToWorld.m[2][3];
    constants.cameraPosition.w = 1.0f;

    constants.ambientColor.x = record->ambientColor.x;
    constants.ambientColor.y = record->ambientColor.y;
    constants.ambientColor.z = record->ambientColor.z;

    constants.toneMapping.x = record->exposure;
    constants.toneMapping.y = record->tonemapWhitePoint;
    constants.toneMapping.z = record->encodeOutputToSRGB ? 1.0f : 0.0f;

    constants.lightParams.x = (f32)record->lightCount;

    void* mapped = Fluxion_RHI_MapBuffer(record->frameConstantBuffer);
    if (mapped != NULL)
    {
        memcpy(mapped, &constants, sizeof(constants));
        Fluxion_RHI_UnmapBuffer(record->frameConstantBuffer);
    }
}

void Fluxion_RenderView_SetLights(FluxionRenderViewHandle view, const FluxionRenderLight* lights, u32 count)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return;
    if (count > 0 && lights == NULL) return;

    // Grown to fit, and never shrunk. A scene whose light count wobbles
    // by one from frame to frame would otherwise rebuild two buffers and
    // a bind group twice a second, and the memory saved by shrinking is
    // sixty-four bytes a light.
    if (count > record->lightCapacity)
    {
        u32 capacity = record->lightCapacity;
        while (capacity < count) capacity *= 2;

        FluxionRHIBufferHandle staging;
        FluxionRHIBufferHandle storage;
        if (!Fluxion_RenderViewInternal_MakeLightStorage(record->device, capacity, &staging, &storage)) return;

        FluxionRHIBindGroupHandle group = Fluxion_RenderViewInternal_MakeFrameBindGroup(
            record->device, record->frameBindGroupLayout, record->frameConstantBuffer, storage, capacity);
        if (!FLUXION_HANDLE_IS_VALID(group))
        {
            Fluxion_RHI_DestroyBuffer(storage);
            Fluxion_RHI_DestroyBuffer(staging);
            return;
        }

        // The old ones go through the ordinary deferred destroy: the GPU
        // may still be reading last frame's list out of them.
        Fluxion_RHI_DestroyBindGroup(record->frameBindGroup);
        Fluxion_RHI_DestroyBuffer(record->lightStorage);
        Fluxion_RHI_DestroyBuffer(record->lightStaging);

        record->lightStaging = staging;
        record->lightStorage = storage;
        record->frameBindGroup = group;
        record->lightCapacity = capacity;
    }

    record->lightCount = count;
    if (count == 0) return;

    FluxionRenderLightGPU* mapped = (FluxionRenderLightGPU*)Fluxion_RHI_MapBuffer(record->lightStaging);
    if (mapped == NULL) return;

    for (u32 i = 0; i < count; ++i)
    {
        const FluxionRenderLight* light = &lights[i];
        FluxionRenderLightGPU* out = &mapped[i];

        out->positionRange[0] = light->position.x;
        out->positionRange[1] = light->position.y;
        out->positionRange[2] = light->position.z;
        out->positionRange[3] = light->range;

        // Normalized here rather than trusted. A direction that came from
        // a scaled object's transform is not unit length, and a shader
        // that assumed it was would light the surface by however wrong
        // the length happened to be -- which looks like the light being
        // the wrong brightness, not like a scale being wrong.
        FluxionVec3 direction = Fluxion_Vec3_Normalize(light->direction);
        out->direction[0] = direction.x;
        out->direction[1] = direction.y;
        out->direction[2] = direction.z;
        out->direction[3] = (f32)light->type;

        out->color[0] = light->color.x;
        out->color[1] = light->color.y;
        out->color[2] = light->color.z;
        out->color[3] = light->innerConeCos;

        out->cone[0] = light->outerConeCos;
        out->cone[1] = 0.0f;
        out->cone[2] = 0.0f;
        out->cone[3] = 0.0f;
    }

    Fluxion_RHI_UnmapBuffer(record->lightStaging);
}

void Fluxion_RenderView_UploadLights(FluxionRenderViewHandle view, FluxionRHICommandListHandle commandList)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return;

    // Nothing to move, and nothing that needs moving: the count in the
    // frame constants is zero, so no shader will read a byte of it.
    if (record->lightCount == 0) return;

    Fluxion_RHI_CommandList_CopyBuffer(commandList, record->lightStaging, 0, record->lightStorage, 0,
                                       (usize)record->lightCount * sizeof(FluxionRenderLightGPU));
}

bool FluxionRendererInternal_RenderView_Get(FluxionRenderViewHandle view, FluxionRenderTargetHandle* outRenderTarget, u32* outLayerMask, FluxionRHIBindGroupHandle* outFrameBindGroup)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return false;

    if (outRenderTarget != NULL) *outRenderTarget = record->renderTarget;
    if (outLayerMask != NULL) *outLayerMask = record->layerMask;
    if (outFrameBindGroup != NULL) *outFrameBindGroup = record->frameBindGroup;
    return true;
}

bool FluxionRendererInternal_RenderView_GetViewport(FluxionRenderViewHandle view, FluxionViewport* outViewport)
{
    FluxionRenderViewRecord* record = Fluxion_RenderViewInternal_Resolve(view);
    if (record == NULL) return false;

    if (outViewport != NULL) *outViewport = record->viewport;
    return true;
}
