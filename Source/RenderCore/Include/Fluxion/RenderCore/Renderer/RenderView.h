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

// What is constant for one frame of one view, exactly as the shaders
// below see it.
//
// The layout is public because it is a contract: Fluxion/Frame.jsl
// declares the same fields in the same order, and the two have to agree
// byte for byte. A field is added at the END and never in the middle --
// a shader that declares only the first of them still reads the right
// bytes, which is what lets an older material go on working.
//
// Every entry is four floats wide even where three would do. Uniform
// blocks round a three-component value up to four anyway, and writing the
// padding down is better than leaving each backend's packing rules to
// decide where the next field starts.
typedef struct FluxionFrameConstants
{
    FluxionMat4 viewProjection;

    // Where the eye is, in world space. w is unused.
    FluxionVec4 cameraPosition;

    // The direction TO the light, not the direction it travels. Unit
    // length; w is unused.
    //
    // To the light, because that is the direction the lighting maths
    // wants and the one a reader can check against a picture: pointing at
    // the sun. Storing the travel direction instead would mean every
    // shader negating it, and one of them forgetting.
    FluxionVec4 sunDirection;

    // Colour times intensity, together. rgb; w is unused.
    FluxionVec4 sunColor;

    // A flat amount of light arriving from everywhere, standing in for
    // the sky until there is one. rgb; w is unused.
    FluxionVec4 ambientColor;
} FluxionFrameConstants;

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

    // One directional light and one flat ambient, which is what a frame
    // can be lit by until there is a light system to hold more.
    //
    // Here rather than somewhere of their own because they are frame
    // frequency: they are the same for every object drawn through this
    // view, which is the definition of what belongs in this buffer. When
    // a real light system arrives it will change how these values GET
    // here and not what a shader reads, because a shader reads
    // Fluxion/Frame.jsl either way.
    FluxionVec3 sunDirection; // to the light; normalized here if it is not already
    FluxionVec3 sunColor;     // colour times intensity
    FluxionVec3 ambientColor;
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
