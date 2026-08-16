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
