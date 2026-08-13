// Plain C -- only appends into FluxionRenderer's CPU-side scratch array;
// the actual GPU upload + draw happens in Renderer.cpp's
// Fluxion_Renderer_EndFrame (see RendererInternal.h's comment on why the
// debug-draw pipeline is built there, in C++, instead of here).

#include <Fluxion/RenderCore/Renderer/DebugDraw.h>

#include "RendererInternal.h"

static void Fluxion_DebugDrawInternal_PushVertex(FluxionRenderer* renderer, FluxionVec3 position, FluxionVec4 color)
{
    if (renderer->debugVertexCount >= FLUXION_RENDERER_MAX_DEBUG_VERTICES) return; // scratch buffer is best-effort -- see RendererInternal.h

    FluxionDebugDrawVertex* vertex = &renderer->debugVertices[renderer->debugVertexCount++];
    vertex->position = position;
    vertex->color = color;
}

void Fluxion_DebugDraw_Line(FluxionRendererHandle rendererHandle, FluxionVec3 a, FluxionVec3 b, FluxionVec4 color)
{
    FluxionRenderer* renderer = (FluxionRenderer*)Fluxion_Renderer_GetForwardOpaquePassUserData(rendererHandle);
    if (renderer == NULL || !renderer->inFrame) return;

    Fluxion_DebugDrawInternal_PushVertex(renderer, a, color);
    Fluxion_DebugDrawInternal_PushVertex(renderer, b, color);
}

void Fluxion_DebugDraw_Triangle(FluxionRendererHandle rendererHandle, FluxionVec3 v0, FluxionVec3 v1, FluxionVec3 v2, FluxionVec4 color)
{
    FluxionRenderer* renderer = (FluxionRenderer*)Fluxion_Renderer_GetForwardOpaquePassUserData(rendererHandle);
    if (renderer == NULL || !renderer->inFrame) return;

    // The built-in debug pipeline draws FLUXION_RHI_PRIMITIVE_TOPOLOGY_LINE_LIST
    // (see Renderer.cpp) -- a triangle is appended as its own 3-edge
    // wireframe outline rather than a filled tri, which is both simpler
    // (no separate topology/pipeline needed) and generally more useful
    // for a debug overlay.
    Fluxion_DebugDrawInternal_PushVertex(renderer, v0, color);
    Fluxion_DebugDrawInternal_PushVertex(renderer, v1, color);
    Fluxion_DebugDrawInternal_PushVertex(renderer, v1, color);
    Fluxion_DebugDrawInternal_PushVertex(renderer, v2, color);
    Fluxion_DebugDrawInternal_PushVertex(renderer, v2, color);
    Fluxion_DebugDrawInternal_PushVertex(renderer, v0, color);
}
