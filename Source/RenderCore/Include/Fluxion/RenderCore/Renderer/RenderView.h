#pragma once

#include <Fluxion/Foundation/Handle.h>
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/RenderTarget.h>

#ifdef __cplusplus
extern "C" {
#endif

// RHI.h has no viewport/scissor-rect type of its own to reuse -- there is
// no Fluxion_RHI_CommandList_SetViewport call anywhere in that contract
// yet, so these stay minimal, Renderer-owned types until the RHI grows a
// real one.
typedef struct FluxionViewport
{
    f32 x, y, width, height;
    f32 minDepth, maxDepth;
} FluxionViewport;

typedef struct FluxionScissorRect
{
    i32 x, y;
    u32 width, height;
} FluxionScissorRect;

FLUXION_DEFINE_HANDLE(FluxionRenderViewHandle);

// `renderPipeline` from the original sketch is deliberately not here -- a
// view doesn't own one; pipeline selection happens per-material/per-pass
// (see FluxionDrawPacket), so an unused field would just sit here idle.
typedef struct FluxionRenderViewDesc
{
    FluxionMat4 viewMatrix;
    FluxionMat4 projectionMatrix;
    FluxionViewport viewport;
    FluxionScissorRect scissor;
    FluxionRenderTargetHandle renderTarget;
    u32 layerMask;
} FluxionRenderViewDesc;

FluxionRenderViewHandle Fluxion_RenderView_Create(FluxionRHIDeviceHandle device, const FluxionRenderViewDesc* desc);
void Fluxion_RenderView_Destroy(FluxionRenderViewHandle view);

// Recomputes viewProjection = projectionMatrix * viewMatrix and uploads
// it to this view's FRAME-frequency uniform buffer. Create does not call
// this itself (a caller may want to set up several views before touching
// the RHI for any one of them) -- call at least once before a draw using
// this view, and again whenever viewMatrix/projectionMatrix change.
void Fluxion_RenderView_UpdateFrameConstants(FluxionRenderViewHandle view);

#ifdef __cplusplus
}
#endif
