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

#include <Fluxion/Assets/AssetRef.h>
#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/RenderCore/Scene/RenderWorld.h>
#include <Fluxion/Scene/Scene.h>

#ifdef __cplusplus
extern "C" {
#endif

// SOMETHING AN OBJECT IS MADE OF, as data rather than as a script call.
//
// Until this existed, the only way an object got drawn was a script
// asking for it every frame -- which meant the drawing was a side effect
// of running code, and a world nobody ticked drew nothing. A component
// says what the object IS, and the frame reads it.
//
// BOTH REFERENCES ARE IDS. A mesh and a material are assets; a handle in
// a saved scene comes back pointing at whatever occupied that slot in
// the next run, which is the whole reason FluxionAssetRef exists.
typedef struct FluxionMeshRenderer
{
    FluxionAssetRef mesh;
    FluxionAssetRef material;

    // Which layers this is drawn in. Zero means every layer, so that a
    // component nobody filled in is visible rather than invisible -- the
    // failure that says something is easier to work out than the one
    // that says nothing.
    u32 layerMask;
} FluxionMeshRenderer;

FluxionTypeId Fluxion_MeshRenderer_TypeId(void);

// Everything in the scene a frame needs, in the shape the renderer takes.
//
// One walk where there were three (lights, camera, and whatever the
// scripts asked to draw), and the result belongs to the renderer rather
// than to the scene: this is the step that stops the ECS being the
// renderer's storage.
//
// `world` must already be initialised; it is cleared here and filled
// from scratch, so the same one is meant to be reused every frame.
//
// AN OBJECT WHOSE ASSETS ARE NOT READY YET IS LEFT OUT. Not drawn half,
// not drawn with somebody else's mesh -- left out, and the frame after
// it is ready it appears. Assets are acquired once per scene and held
// (see Fluxion_Scene_ReleaseRenderAssets), because acquiring and
// releasing per frame would unload and reload anything that fell to
// zero references between two frames.
//
// False when the scene has no camera, which is a frame nobody can draw;
// the objects and lights are still filled in.
bool Fluxion_Scene_ExtractRenderWorld(FluxionSceneHandle scene, f32 aspect, FluxionRenderWorld* world);

// Lets go of every asset the extraction acquired for this scene.
//
// Called for you when a scene is destroyed. A program that swaps most of
// a scene's contents and wants the old assets gone before then may call
// it itself -- the next extraction acquires whatever it still needs.
void Fluxion_Scene_ReleaseRenderAssets(FluxionSceneHandle scene);

#ifdef __cplusplus
}
#endif
