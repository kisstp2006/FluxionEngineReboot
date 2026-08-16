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

// Where each shadow-casting light's depth map lives inside one texture.
//
// ONE TEXTURE FOR EVERY SHADOW, not one per light: a shader can read a
// handful of textures, and a scene can hold far more lights than that.
// Every shadow sampled in a frame comes out of this one, which is what
// makes the number of lights a budget rather than a hard limit in the
// binding model.
//
// A UNIFORM GRID, not a general rectangle packer. Every tile is the same
// size, so there is no fragmentation to reason about and no packing
// heuristic anybody has to trust -- and this file stays something that
// can be checked by reading it. Shadows of different qualities are a
// later question, and a packer is what it will need; nothing here is in
// its way.
//
// KNOWS NOTHING ABOUT LIGHTS OR THE RHI. It counts tiles. What asks for
// them, and what draws into them, are elsewhere -- which is why this can
// be tested to the last case without a device.

// The most tiles a single request can ask for: a point light needs one
// per cube face, and nothing needs more.
#define FLUXION_SHADOW_ATLAS_MAX_TILES_PER_REQUEST 6

typedef struct FluxionShadowAtlasDesc
{
    // Both in texels, both a side of a square. `atlasSize` must be a
    // whole number of tiles across, or nothing is allocated -- a
    // remainder would be atlas nobody can address.
    u32 atlasSize;
    u32 tileSize;
} FluxionShadowAtlasDesc;

typedef struct FluxionShadowAtlasRequest
{
    // 1 for a spot light or one cascade of the sun, 6 for a point light.
    u32 tileCount;

    // The caller's own name for what asked. Handed back untouched, so
    // the caller can match answers to requests without depending on the
    // order they come back in -- which is not the order they went in,
    // since the largest requests are placed first.
    u32 tag;
} FluxionShadowAtlasRequest;

// Where one tile sits, in tiles rather than texels: the grid is what
// this file reasons in, and texels are one multiplication away.
typedef struct FluxionShadowAtlasTile
{
    u32 x;
    u32 y;
} FluxionShadowAtlasTile;

typedef struct FluxionShadowAtlasResult
{
    u32 tag;

    // False means this light casts no shadow this frame. SAID, not
    // silently dropped: a light that keeps its shadow while another
    // loses one is a decision, and a caller that cannot see it made
    // cannot report it or choose differently next frame.
    bool fitted;

    FluxionShadowAtlasTile tiles[FLUXION_SHADOW_ATLAS_MAX_TILES_PER_REQUEST];
    u32 tileCount; // as requested when fitted, zero otherwise
} FluxionShadowAtlasResult;

// Places every request it can, and returns how many it placed.
//
// `outResults` holds one entry per request, in the order the requests
// came in -- including the ones that did not fit, which is what makes
// the count above a summary rather than the only answer.
//
// The largest requests go first. A point light's six tiles are harder to
// place than a cascade's one, and letting the small ones go first would
// leave the atlas full of gaps too small for anything that needed room.
//
// Deterministic: the same requests in the same order always produce the
// same placement, so a frame that changed nothing does not move its
// shadows -- which would look like the scene shimmering.
u32 Fluxion_ShadowAtlas_Allocate(const FluxionShadowAtlasDesc* desc,
                                 const FluxionShadowAtlasRequest* requests, u32 requestCount,
                                 FluxionShadowAtlasResult* outResults);

// The part of the atlas one tile covers, as a 0..1 rectangle: xy is the
// corner, zw the size. This is what a shader needs to turn a coordinate
// in one light's shadow map into a coordinate in the atlas, and what a
// viewport needs scaled by the atlas size.
FluxionVec4 Fluxion_ShadowAtlas_TileRect(const FluxionShadowAtlasDesc* desc, FluxionShadowAtlasTile tile);

#ifdef __cplusplus
}
#endif
