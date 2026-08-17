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

#ifdef __cplusplus
extern "C" {
#endif

// Where a light looks from, as a matrix.
//
// A shadow map is a depth buffer rendered from the light, so every kind
// of light needs the pair of matrices a camera needs -- and each kind
// needs a different pair. The sun sees a slab of the world with no
// perspective at all; a spot sees a cone; a point sees six squares.
//
// Maths from published descriptions; see SHADER_SOURCES.md. Everything
// here is arithmetic on numbers the caller supplies, so it can be read
// against its answer rather than looked at on a screen -- which is why
// it is its own file rather than lines inside a pass.
//
// THE SAME CLIP CONVENTION AS EVERY OTHER MATRIX HERE: depth comes out
// in 0..1, zero at the near plane. Two of the three backends take that
// natively and the third is told to (see the OpenGL backend's clip
// control). A shadow matrix that disagreed would not fail -- it would
// put every shadow in the wrong place along the light's axis, or throw
// half of them away.

// How many cascades the sun may be split into.
#define FLUXION_SHADOW_MAX_CASCADES 8

// The sun's view-projection for one slab of the world.
//
// FITTED TO A SPHERE, not to the camera frustum's corners, and that is
// the choice that keeps shadow edges still. A frustum fitted tightly
// changes shape as the camera turns, so every texel covers a different
// piece of world from one frame to the next and the edges crawl. A
// sphere is the same size whichever way the camera faces.
//
// `tileSize` is the shadow map's resolution in texels, and it is not
// decoration: the slab's centre is snapped to whole texels, so that a
// camera moving slowly moves the shadow map in texel steps rather than
// sliding it under the samples -- the other half of the same problem.
//
// `direction` is THE WAY THE LIGHT TRAVELS, the same convention as
// every light in this engine.
FluxionMat4 Fluxion_ShadowMatrices_Directional(FluxionVec3 direction, FluxionVec3 centre, f32 radius, u32 tileSize);

// Where to cut the camera's depth range into cascades.
//
// Writes `count + 1` distances, beginning at `nearPlane` and ending at
// `farPlane`, so cascade i covers outSplits[i] to outSplits[i + 1].
//
// `blend` mixes two schemes that are each wrong alone. Splitting evenly
// gives the near cascades far too little of the range, where almost
// every pixel is; splitting logarithmically -- which is what an even
// share of PERCEIVED detail wants -- gives the far ones slivers too thin
// to be worth a whole map. Zero is even, one is logarithmic, and the
// published recommendation is most of the way towards logarithmic.
//
// Returns false and writes nothing for a count of zero, a count past
// FLUXION_SHADOW_MAX_CASCADES, or a range that is not positive.
bool Fluxion_ShadowMatrices_CascadeSplits(f32 nearPlane, f32 farPlane, u32 count, f32 blend, f32* outSplits);

// The sphere holding one slice of what the camera can see.
//
// This is what the sun's matrix above wants, and working it out is the
// step between a pair of split distances and a matrix. A SPHERE rather
// than the eight corners, for the reason the matrix gives: a sphere is
// the same size whichever way the camera faces, and a slab fitted to it
// therefore does not change shape as the camera turns.
//
// Exact, not an estimate: the sphere returned touches the slice's near
// and far corners together, or the far corners alone where those already
// enclose the rest.
//
// `forward` is the way the camera looks; it is normalized here. The
// radius is written through `outRadius` and is never zero.
FluxionVec3 Fluxion_ShadowMatrices_CascadeSphere(FluxionVec3 cameraPosition, FluxionVec3 forward,
                                                 f32 fovYRadians, f32 aspect,
                                                 f32 sliceNear, f32 sliceFar, f32* outRadius);

// The two offsets a sun shadow of this size needs.
//
// Both come from one number: how much world a single texel of the map
// covers, which is the whole of why a lit surface shadows itself. They
// live here rather than at the caller because it is this file that
// decides the slab a shadow's depth is measured in -- a bias chosen
// anywhere else would be a number that happened to work.
//
// Writes nothing through a null pointer, so a caller may ask for one.
void Fluxion_ShadowMatrices_DirectionalBias(f32 radius, u32 tileSize, f32* outDepthBias, f32* outNormalBias);

// A spot light's view-projection.
//
// The cone becomes the field of view, doubled: the angle names the
// half-angle from the axis, and a projection is told the whole angle it
// spans. `range` is where the light's contribution reaches zero, which
// is exactly as far as its shadow needs to reach.
FluxionMat4 Fluxion_ShadowMatrices_Spot(FluxionVec3 position, FluxionVec3 direction, f32 outerConeAngle, f32 range);

// One of a point light's six views.
//
// `face` is 0..5 in the same order as every cube map here: +X, -X, +Y,
// -Y, +Z, -Z. Ninety degrees each way, so the six together see
// everything exactly once -- which is why a point light costs six tiles
// and not one.
FluxionMat4 Fluxion_ShadowMatrices_PointFace(FluxionVec3 position, u32 face, f32 range);

#ifdef __cplusplus
}
#endif
