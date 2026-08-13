#pragma once

#include <Fluxion/Foundation/Math.h>
#include <Fluxion/RenderCore/Renderer/Renderer.h>

#ifdef __cplusplus
extern "C" {
#endif

// Accumulates into a small per-frame scratch buffer, drawn through the
// renderer's own tiny, self-contained line/triangle pipeline (its own
// embedded shader source, built once at Fluxion_Renderer_Create) instead
// of going through ShaderProgram/Material -- see DebugDraw.c for why.
// Valid only between Fluxion_Renderer_BeginFrame and
// Fluxion_Renderer_EndFrame; a call outside that window is dropped.
void Fluxion_DebugDraw_Line(FluxionRendererHandle renderer, FluxionVec3 a, FluxionVec3 b, FluxionVec4 color);
void Fluxion_DebugDraw_Triangle(FluxionRendererHandle renderer, FluxionVec3 v0, FluxionVec3 v1, FluxionVec3 v2, FluxionVec4 color);

#ifdef __cplusplus
}
#endif
