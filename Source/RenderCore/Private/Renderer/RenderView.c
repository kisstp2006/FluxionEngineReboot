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

    FluxionVec3 sunDirection;
    FluxionVec3 sunColor;
    FluxionVec3 ambientColor;
    f32 exposure;
    f32 tonemapWhitePoint;
    bool encodeOutputToSRGB;

    FluxionRHIBufferHandle frameConstantBuffer; // uniform buffer holding this frame's FluxionFrameConstants
    FluxionRHIBindGroupLayoutHandle frameBindGroupLayout;
    FluxionRHIBindGroupHandle frameBindGroup;
} FluxionRenderViewRecord;

static FluxionRenderViewRecord s_renderViews[FLUXION_RENDERER_MAX_RENDER_VIEWS];

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

    FluxionRHIBindGroupEntry entry;
    entry.binding = 0;
    entry.type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    entry.buffer = frameConstantBuffer;
    entry.bufferOffset = 0;
    entry.bufferSize = sizeof(FluxionFrameConstants);
    entry.textureView = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    entry.sampler = (FluxionRHISamplerHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionRHIBindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = frameBindGroupLayout;
    bindGroupDesc.entries = &entry;
    bindGroupDesc.entryCount = 1;
    FluxionRHIBindGroupHandle frameBindGroup = Fluxion_RHI_CreateBindGroup(device, &bindGroupDesc);

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
    record->sunDirection = desc->sunDirection;
    record->sunColor = desc->sunColor;
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

    const FluxionVec3 sun = Fluxion_Vec3_Normalize(record->sunDirection);
    constants.sunDirection.x = sun.x;
    constants.sunDirection.y = sun.y;
    constants.sunDirection.z = sun.z;

    constants.sunColor.x = record->sunColor.x;
    constants.sunColor.y = record->sunColor.y;
    constants.sunColor.z = record->sunColor.z;

    constants.ambientColor.x = record->ambientColor.x;
    constants.ambientColor.y = record->ambientColor.y;
    constants.ambientColor.z = record->ambientColor.z;

    constants.toneMapping.x = record->exposure;
    constants.toneMapping.y = record->tonemapWhitePoint;
    constants.toneMapping.z = record->encodeOutputToSRGB ? 1.0f : 0.0f;

    void* mapped = Fluxion_RHI_MapBuffer(record->frameConstantBuffer);
    if (mapped != NULL)
    {
        memcpy(mapped, &constants, sizeof(constants));
        Fluxion_RHI_UnmapBuffer(record->frameConstantBuffer);
    }
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
