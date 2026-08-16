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

// Handing out tiles of one shadow texture. See the header for why a
// uniform grid, and why this knows nothing about lights.

#include <Fluxion/RenderCore/Renderer/ShadowAtlas.h>

#include <string.h>

// How many requests one call will consider. Beyond this the extras are
// reported as not fitting rather than ignored: a caller with more
// shadow-casting lights than this has a budget problem, not a
// correctness one, and it should see that in the results.
#define FLUXION_SHADOW_ATLAS_MAX_REQUESTS 256

u32 Fluxion_ShadowAtlas_Allocate(const FluxionShadowAtlasDesc* desc,
                                 const FluxionShadowAtlasRequest* requests, u32 requestCount,
                                 FluxionShadowAtlasResult* outResults)
{
    if (desc == NULL || outResults == NULL) return 0;
    if (requestCount > 0 && requests == NULL) return 0;

    // Every result starts as "did not fit", so a request skipped by any
    // path below is a light without a shadow rather than a light holding
    // whatever was in the caller's memory.
    for (u32 i = 0; i < requestCount; ++i)
    {
        memset(&outResults[i], 0, sizeof(outResults[i]));
        outResults[i].tag = requests[i].tag;
    }

    if (desc->tileSize == 0 || desc->atlasSize < desc->tileSize) return 0;

    // A remainder would be atlas nobody can address, so a size that is
    // not a whole number of tiles is refused rather than rounded -- a
    // rounded one hands back tiles that fall off the edge.
    if (desc->atlasSize % desc->tileSize != 0) return 0;

    const u32 tilesPerSide = desc->atlasSize / desc->tileSize;
    const u32 tileTotal = tilesPerSide * tilesPerSide;

    // The order requests are CONSIDERED in, largest first -- the results
    // still come back in the caller's order. Six tiles are harder to
    // place than one, and letting the ones go first would leave the
    // atlas full of gaps too small for anything that needed room.
    //
    // A selection pass rather than a sort: the count is small, this runs
    // once a frame, and it keeps requests of equal size in their
    // original order, which is what makes the placement repeatable.
    u32 order[FLUXION_SHADOW_ATLAS_MAX_REQUESTS];
    u32 orderCount = 0;
    const u32 considered = requestCount < FLUXION_SHADOW_ATLAS_MAX_REQUESTS ? requestCount : FLUXION_SHADOW_ATLAS_MAX_REQUESTS;

    for (u32 size = FLUXION_SHADOW_ATLAS_MAX_TILES_PER_REQUEST; size >= 1; --size)
    {
        for (u32 i = 0; i < considered; ++i)
        {
            if (requests[i].tileCount == size) order[orderCount++] = i;
        }
    }

    u32 nextTile = 0;
    u32 fittedCount = 0;

    for (u32 slot = 0; slot < orderCount; ++slot)
    {
        const u32 index = order[slot];
        const u32 wanted = requests[index].tileCount;

        // Room for all of them or none: a point light with four of its
        // six faces is not a partly-shadowed light, it is a broken one.
        if (nextTile + wanted > tileTotal) continue;

        for (u32 tile = 0; tile < wanted; ++tile)
        {
            const u32 position = nextTile + tile;
            outResults[index].tiles[tile].x = position % tilesPerSide;
            outResults[index].tiles[tile].y = position / tilesPerSide;
        }

        outResults[index].fitted = true;
        outResults[index].tileCount = wanted;
        nextTile += wanted;
        ++fittedCount;
    }

    return fittedCount;
}

FluxionVec4 Fluxion_ShadowAtlas_TileRect(const FluxionShadowAtlasDesc* desc, FluxionShadowAtlasTile tile)
{
    FluxionVec4 rect = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (desc == NULL || desc->tileSize == 0 || desc->atlasSize == 0) return rect;

    const f32 scale = (f32)desc->tileSize / (f32)desc->atlasSize;
    rect.x = (f32)tile.x * scale;
    rect.y = (f32)tile.y * scale;
    rect.z = scale;
    rect.w = scale;
    return rect;
}
