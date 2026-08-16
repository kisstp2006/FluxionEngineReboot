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
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Scene/Scene.h>

// Declared rather than included: this header describes what a light IS,
// and the shape a renderer wants one in belongs to the renderer.
struct FluxionRenderLight;

#ifdef __cplusplus
extern "C" {
#endif

// A light is something an object HAS: it can be moved, turned off,
// copied, saved and read back -- none of which a field on a view can do.
//
// WHERE THE LIGHT IS DOES NOT LIVE HERE: position and direction come
// from the object's transform, never repeated in the component -- two
// places saying where a light is can disagree, silently. A directional
// light shines along the object's forward axis; a point sits at its
// position; a spot does both.
//
// THE COLOUR IS THE INTENSITY -- no separate brightness field, here or
// anywhere else: twice as bright IS twice the light, which is what makes
// a real camera exposure meaningful (Fluxion_Exposure_FromCamera).

// Light that arrives from one direction, everywhere, with no falloff --
// a sun. Distance to it is not a thing that exists.
typedef struct FluxionDirectionalLight
{
    FluxionVec3 color;
} FluxionDirectionalLight;

// Light from a point in space, spreading in every direction.
typedef struct FluxionPointLight
{
    FluxionVec3 color;

    // Where this light's contribution reaches zero.
    //
    // Not a physical quantity -- real light never stops -- but a
    // necessary one: without a distance at which a light can be ignored,
    // every light has to be considered for every pixel, and that is what
    // makes many lights unaffordable. The falloff is shaped so that it
    // arrives at exactly zero here rather than merely small, because a
    // light dropped while it was still contributing leaves a visible edge
    // where it was dropped.
    f32 range;
} FluxionPointLight;

// A point light with a cone over it.
typedef struct FluxionSpotLight
{
    FluxionVec3 color;
    f32 range;

    // Radians from the axis. Full brightness within the inner angle,
    // nothing beyond the outer, and a smooth transition between them.
    //
    // Two angles rather than one and a softness, because that is how the
    // two are authored and how every exchange format stores them -- and a
    // softness would have to be turned back into these to be used.
    f32 innerConeAngle;
    f32 outerConeAngle;
} FluxionSpotLight;

// What the world looks like in every direction: a sky, and the light it
// casts. A LIGHT, not a background -- usually the largest light source
// in the scene, the reason a shaded side is blue outdoors; drawing it
// behind everything is a consequence, not the purpose.
//
// AN ASSET REFERENCE, not a texture handle: a handle in a saved scene
// would come back pointing at whatever occupied the slot next. Not a
// separate asset kind either -- a sky is a Texture whose dimension is a
// cube; six images, equirectangular or a cross are ways of IMPORTING
// one, and the importer lives in a plugin.
typedef struct FluxionEnvironmentLight
{
    FluxionAssetRef environment;

    // A multiplier over what the asset holds. The environment is stored
    // in real units like everything else, so this is not a brightness
    // slider: it is for a scene that wants the same sky dimmer than it
    // was captured, without a second copy of it.
    f32 intensity;
} FluxionEnvironmentLight;

// The ids these are registered under. A script naming the same type names
// the same id, so a script's light and a stored light are one thing.
FluxionTypeId Fluxion_DirectionalLight_TypeId(void);
FluxionTypeId Fluxion_PointLight_TypeId(void);
FluxionTypeId Fluxion_SpotLight_TypeId(void);
FluxionTypeId Fluxion_EnvironmentLight_TypeId(void);

// Every light in the scene, flattened into world space, in the shape a
// renderer takes.
//
// Returns HOW MANY THERE ARE, not how many fitted -- a caller sizes its
// next call from this, so answering with what it could hold would mean it
// never learned it needed more. Call with a null buffer to ask only the
// count.
//
// Reads the world matrices as they stand, so it belongs after the
// transform update and before the frame is drawn.
u32 Fluxion_Scene_GatherLights(FluxionSceneHandle scene, struct FluxionRenderLight* outLights, u32 capacity);

// The scene's sky, if it has one.
//
// One rather than a list: a scene is inside one world, and two skies is
// not a thing that has an answer. When more than one object carries the
// component the first found wins and the rest are reported -- silently
// picking one would make which sky you got depend on the order objects
// happened to be created in.
//
// False when nothing in the scene carries the component, which is not an
// error: a scene with no sky is lit by its lights alone.
bool Fluxion_Scene_GatherEnvironment(FluxionSceneHandle scene, FluxionEnvironmentLight* outEnvironment);

#ifdef __cplusplus
}
#endif
