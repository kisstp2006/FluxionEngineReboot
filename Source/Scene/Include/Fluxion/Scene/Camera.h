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
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Scene/Scene.h>

#ifdef __cplusplus
extern "C" {
#endif

// The camera is something an object HAS, like a light: it can be moved,
// parented, saved and read back, and a script reaches it through the same
// reflection every other component uses. WHERE it is and WHICH WAY it
// looks come from the object's transform (forward is negative Z) and are
// not repeated here -- two places saying where a camera is can disagree,
// silently.
//
// The aspect ratio is deliberately absent: it belongs to the surface
// being rendered into, which the scene cannot know. It is handed in when
// the matrices are asked for.
typedef struct FluxionCamera
{
    // The vertical field of view, in radians.
    f32 fovYRadians;

    f32 nearPlane;
    f32 farPlane;

    // Which render pipeline asset draws what this camera sees. Nil --
    // which is what a camera nobody set one on has -- means the
    // project's default, so the common case is the one nobody has to
    // fill in.
    //
    // A reference rather than anything the renderer hands out, for the
    // reason every asset field here is one: this gets saved with the
    // scene, and a handle means where something sat in a table during
    // one run of one program.
    //
    // THIS IS THE WHOLE OF WHAT A CAMERA KNOWS ABOUT RENDERING. It does
    // not know whether the pipeline it names is forward or deferred,
    // what passes it has, or what it costs -- so a scene keeps working
    // when the answer to any of those changes.
    FluxionAssetRef renderPipeline;
} FluxionCamera;

FluxionTypeId Fluxion_Camera_TypeId(void);

// The scene's camera, as the two matrices a render view takes -- ordinary
// row-major, the renderer owns the upload byte order.
//
// One camera, like one environment: the first found wins and extras are
// reported. False when no object carries one, and the outputs are then
// left alone.
bool Fluxion_Scene_GatherCamera(FluxionSceneHandle scene, f32 aspect, FluxionMat4* outView, FluxionMat4* outProjection);

// What that same camera says about which pipeline draws it.
//
// Its own function rather than two more outputs on the one above,
// because a caller that only wants the matrices is the common one and a
// caller that only wants this -- deciding, before a view exists, what
// kind of view to build -- is a real one too. The same camera answers
// both: the first found, as above.
//
// False when nothing in the scene carries a camera. The reference that
// comes back may still be nil, which is the camera saying "whatever the
// project uses" -- Fluxion_RenderPipelineAsset_Resolve is what turns
// those two into one answer.
bool Fluxion_Scene_GatherCameraRenderPipeline(FluxionSceneHandle scene, FluxionAssetRef* outPipeline);

#ifdef __cplusplus
}
#endif
