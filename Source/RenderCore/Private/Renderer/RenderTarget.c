#include <Fluxion/RenderCore/Renderer/RenderTarget.h>

#include "RendererInternal.h"

#include <Fluxion/Foundation/Assert.h>

#include <string.h>

typedef struct FluxionRenderTargetRecord
{
    bool alive;
    u32 generation;

    FluxionRHITextureViewHandle colorViews[FLUXION_RENDER_TARGET_MAX_COLOR_ATTACHMENTS];
    u32 colorViewCount;
    FluxionRHITextureViewHandle depthView;
} FluxionRenderTargetRecord;

static FluxionRenderTargetRecord s_renderTargets[FLUXION_RENDERER_MAX_RENDER_TARGETS];

static FluxionRenderTargetRecord* Fluxion_RenderTargetInternal_Resolve(FluxionRenderTargetHandle handle)
{
    if (handle.index >= FLUXION_RENDERER_MAX_RENDER_TARGETS) return NULL;
    FluxionRenderTargetRecord* record = &s_renderTargets[handle.index];
    if (!record->alive || record->generation != handle.generation) return NULL;
    return record;
}

FluxionRenderTargetHandle Fluxion_RenderTarget_Create(FluxionRHIDeviceHandle device, const FluxionRenderTargetDesc* desc)
{
    FLUXION_UNUSED(device); // nothing device-owned is created here -- the caller already owns every view this wraps

    FluxionRenderTargetHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(desc != NULL && desc->colorViewCount <= FLUXION_RENDER_TARGET_MAX_COLOR_ATTACHMENTS);
    if (desc == NULL || desc->colorViewCount > FLUXION_RENDER_TARGET_MAX_COLOR_ATTACHMENTS) return invalid;

    u32 index = FLUXION_RENDERER_MAX_RENDER_TARGETS;
    for (u32 i = 0; i < FLUXION_RENDERER_MAX_RENDER_TARGETS; ++i)
    {
        if (!s_renderTargets[i].alive) { index = i; break; }
    }
    if (index == FLUXION_RENDERER_MAX_RENDER_TARGETS) return invalid;

    FluxionRenderTargetRecord* record = &s_renderTargets[index];
    u32 generation = record->generation;
    memset(record, 0, sizeof(*record));
    record->alive = true;
    record->generation = generation;
    for (u32 i = 0; i < desc->colorViewCount; ++i) record->colorViews[i] = desc->colorViews[i];
    record->colorViewCount = desc->colorViewCount;
    record->depthView = desc->depthView;

    FluxionRenderTargetHandle handle = { index, generation };
    return handle;
}

void Fluxion_RenderTarget_Destroy(FluxionRenderTargetHandle target)
{
    FluxionRenderTargetRecord* record = Fluxion_RenderTargetInternal_Resolve(target);
    if (record == NULL)
    {
        FLUXION_ASSERT_MSG(false, "Fluxion_RenderTarget_Destroy called with an invalid or already-destroyed handle");
        return;
    }

    // Owns no RHI object of its own -- the texture views it references
    // were created (and are destroyed) by whoever owns the underlying
    // textures.
    record->alive = false;
    ++record->generation;
}

bool FluxionRendererInternal_RenderTarget_Get(FluxionRenderTargetHandle target, FluxionRHITextureViewHandle* outColorViews, u32* outColorViewCount, FluxionRHITextureViewHandle* outDepthView)
{
    FluxionRenderTargetRecord* record = Fluxion_RenderTargetInternal_Resolve(target);
    if (record == NULL) return false;

    if (outColorViews != NULL)
    {
        for (u32 i = 0; i < record->colorViewCount; ++i) outColorViews[i] = record->colorViews[i];
    }
    if (outColorViewCount != NULL) *outColorViewCount = record->colorViewCount;
    if (outDepthView != NULL) *outDepthView = record->depthView;
    return true;
}
