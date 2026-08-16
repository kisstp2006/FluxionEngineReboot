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

#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RHI/RHI.h>

#ifdef __cplusplus
extern "C" {
#endif

// Two things a renderer needs before any material has been written, and
// which are here together because they answer the same question: what
// does a draw bind when the material has nothing of its own to bind?
//
// A material that uses no base colour map still has a base colour map
// binding in its bind group -- the shader declared one, so the layout has
// one, and a group with a hole in it is refused outright by the backend.
// So the answer cannot be "nothing"; it has to be a texture that changes
// nothing.

// The one-pixel textures every material falls back to.
//
// Each is chosen so that binding it is the same as not having one at all:
// white multiplies to no change, black adds none, and the flat normal
// points straight out of the surface. A single missing map should look
// like a missing map and not like a black object, which is what a zeroed
// texture would give.
typedef enum FluxionDefaultTexture
{
    FLUXION_DEFAULT_TEXTURE_WHITE = 0,
    FLUXION_DEFAULT_TEXTURE_BLACK,

    // (0.5, 0.5, 1) -- the normal that leaves the surface alone. Stored
    // in a linear format, because a normal is a direction and not a
    // colour: read as sRGB it would bend every surface it touched.
    FLUXION_DEFAULT_TEXTURE_FLAT_NORMAL,

    // Six black faces, in the format an environment is stored in.
    //
    // Bound wherever a view has been given no environment of its own, and
    // that is not politeness: a shader cannot ask whether a texture is
    // there, and a backend handed an empty slot refuses the whole bind
    // group rather than the one binding. Black adds nothing, so a scene
    // with no sky looks like a scene with no sky.
    FLUXION_DEFAULT_TEXTURE_BLACK_CUBE,

    FLUXION_DEFAULT_TEXTURE_COUNT,
} FluxionDefaultTexture;

// Made once, on first use, and kept until Shutdown. A device is needed
// only to make them.
bool Fluxion_TextureDefaults_Init(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue);
void Fluxion_TextureDefaults_Shutdown(void);

// An invalid handle before Init or for a value out of range -- never a
// different texture, because a wrong one binds successfully and looks
// like a material bug.
FluxionRHITextureViewHandle Fluxion_TextureDefaults_GetView(FluxionDefaultTexture which);

// --- Samplers -------------------------------------------------------------
//
// Samplers are described by a handful of numbers, and a project uses very
// few distinct combinations while having very many textures. Made once
// per description and handed out again after that: a sampler per texture
// would spend a limited resource on a large number of identical objects.

// The sampler for this description, made if this is the first time it has
// been asked for. An invalid handle if the cache is full or the device
// refuses it.
//
// The returned sampler belongs to the cache and must not be destroyed by
// the caller -- Shutdown destroys every one of them.
FluxionRHISamplerHandle Fluxion_SamplerCache_Get(FluxionRHIDeviceHandle device, const FluxionRHISamplerDesc* desc);

// How many distinct samplers exist. What a test asserts on to show the
// cache is a cache: asking twice for the same description must not make
// this go up.
u32 Fluxion_SamplerCache_GetCount(void);

void Fluxion_SamplerCache_Shutdown(void);

#ifdef __cplusplus
}
#endif
