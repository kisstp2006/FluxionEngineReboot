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

#pragma once

#include <Fluxion/Foundation/Math.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RenderCore/Renderer/Material.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>
#include <Fluxion/RenderCore/Renderer/RenderPipeline.h>
#include <Fluxion/RenderCore/Renderer/RenderView.h>

#ifdef __cplusplus
extern "C" {
#endif

// WHAT A FRAME NEEDS TO KNOW ABOUT A WORLD, AND NOTHING ELSE.
//
// The renderer used to read the scene as it drew: a script asked for a
// draw, the draw went straight into the frame, and there was nowhere in
// between for anything to happen. Nowhere to decide what is visible,
// nowhere to choose a level of detail, nowhere to keep the previous
// frame's answer.
//
// This is that place. A flat list of what is drawable, extracted once
// per frame from whatever holds the objects -- and it knows nothing
// about entities, components or scenes, which is exactly why the
// renderer may depend on it.
//
// Filling it is somebody else's job: Fluxion_Scene_ExtractRenderWorld
// does it from a scene, and a program with no scene at all can fill one
// by hand.

typedef struct FluxionRenderObject
{
    // The world matrix as the transform left it -- ordinary row-major.
    // What the device reads is its transpose, and that happens at the
    // one boundary that uploads it.
    FluxionMat4 transform;

    FluxionMeshBufferHandle mesh;
    FluxionMaterialHandle material;
    FluxionRenderPipelineHandle pipeline;

    u32 layerMask;

    // WHICH LEVEL OF DETAIL, AND WHETHER IT IS SEEN AT ALL.
    //
    // Both are here now, and both are filled in the same way by
    // everything that fills this today: level zero, and visible. What
    // decides them is a step of its own that does not exist yet -- and
    // a field that arrived with that step would change every piece of
    // code that had been written around its absence.
    u32 lodIndex;
    bool visible;
} FluxionRenderObject;

// The camera a frame is drawn through, in the shape a render view takes.
typedef struct FluxionRenderWorldCamera
{
    FluxionMat4 view;
    FluxionMat4 projection;

    // False when nothing in the world carries a camera -- which is not
    // an error here, only a frame nobody can draw.
    bool valid;
} FluxionRenderWorldCamera;

// As many lights as a view can hold, which is what makes this the right
// place to stop counting them.
#define FLUXION_RENDER_WORLD_MAX_LIGHTS 64

typedef struct FluxionRenderWorld
{
    // Grown by doubling and never shrunk, like every other per-frame
    // list in this engine.
    FluxionRenderObject* objects;
    u32 objectCount;
    u32 objectCapacity;

    FluxionRenderLight lights[FLUXION_RENDER_WORLD_MAX_LIGHTS];
    u32 lightCount;

    // How many lights the world actually has, which may be more than
    // fitted. Reported rather than silently equal to the count above:
    // a scene that quietly lost its twelfth light is a scene nobody can
    // debug.
    u32 lightsInWorld;

    FluxionRenderWorldCamera camera;
} FluxionRenderWorld;

// Zeroes one and gives it its first allocation. A world that is never
// initialised is not usable; one that is initialised twice leaks the
// first allocation, so it is initialise-then-shutdown like everything
// else here.
bool Fluxion_RenderWorld_Init(FluxionRenderWorld* world);
void Fluxion_RenderWorld_Shutdown(FluxionRenderWorld* world);

// Forgets last frame's objects and lights, keeping the memory.
void Fluxion_RenderWorld_Clear(FluxionRenderWorld* world);

// Adds one drawable. False only when there is no room to grow into, in
// which case the object is not drawn and that is said.
bool Fluxion_RenderWorld_AddObject(FluxionRenderWorld* world, const FluxionRenderObject* object);

#ifdef __cplusplus
}
#endif
