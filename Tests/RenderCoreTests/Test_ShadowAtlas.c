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

#include "TestFramework.h"

#include <Fluxion/RenderCore/Renderer/ShadowAtlas.h>

#include <string.h>

// Handing out tiles, checked to the last case -- and with no device
// anywhere in it, which is the reason this is its own step: what decides
// which light keeps its shadow is arithmetic, and arithmetic can be read
// against its answer rather than looked at on a screen.

// A four-by-four grid: one row for the sun's four cascades, twelve tiles
// left for everything else.
static FluxionShadowAtlasDesc DefaultDesc(void)
{
    FluxionShadowAtlasDesc desc;
    desc.atlasSize = 4096;
    desc.tileSize = 1024;
    return desc;
}

static bool RectNear(FluxionVec4 rect, f32 x, f32 y, f32 size)
{
    const f32 tolerance = 0.0001f;
    return (rect.x > x - tolerance && rect.x < x + tolerance) &&
           (rect.y > y - tolerance && rect.y < y + tolerance) &&
           (rect.z > size - tolerance && rect.z < size + tolerance) &&
           (rect.w > size - tolerance && rect.w < size + tolerance);
}

void Test_ShadowAtlas_Run(TestContext* ctx)
{
    const FluxionShadowAtlasDesc desc = DefaultDesc();

    // --- Nothing to place -------------------------------------------------
    {
        FluxionShadowAtlasResult results[1];
        TEST_CHECK(ctx, Fluxion_ShadowAtlas_Allocate(&desc, NULL, 0, results) == 0);
    }

    // --- One request takes the first tile ---------------------------------
    {
        const FluxionShadowAtlasRequest requests[1] = { { 1, 7 } };
        FluxionShadowAtlasResult results[1];

        TEST_CHECK(ctx, Fluxion_ShadowAtlas_Allocate(&desc, requests, 1, results) == 1);
        TEST_CHECK(ctx, results[0].fitted);
        TEST_CHECK(ctx, results[0].tag == 7);
        TEST_CHECK(ctx, results[0].tileCount == 1);
        TEST_CHECK(ctx, results[0].tiles[0].x == 0 && results[0].tiles[0].y == 0);
    }

    // --- Exactly full, then one too many ----------------------------------
    //
    // Sixteen tiles in a four-by-four grid, so the seventeenth has
    // nowhere to go. The check is that it SAYS so: a caller that cannot
    // see a light lose its shadow cannot report it or choose otherwise.
    {
        FluxionShadowAtlasRequest requests[17];
        FluxionShadowAtlasResult results[17];
        for (u32 i = 0; i < 17; ++i)
        {
            requests[i].tileCount = 1;
            requests[i].tag = i;
        }

        TEST_CHECK(ctx, Fluxion_ShadowAtlas_Allocate(&desc, requests, 16, results) == 16);
        for (u32 i = 0; i < 16; ++i) TEST_CHECK(ctx, results[i].fitted);

        TEST_CHECK(ctx, Fluxion_ShadowAtlas_Allocate(&desc, requests, 17, results) == 16);
        TEST_CHECK(ctx, !results[16].fitted);
        TEST_CHECK(ctx, results[16].tileCount == 0);

        // Still named, so the caller can say WHICH light went dark.
        TEST_CHECK(ctx, results[16].tag == 16);
    }

    // --- A point light's six faces are placed together or not at all ------
    {
        const FluxionShadowAtlasRequest requests[1] = { { 6, 42 } };
        FluxionShadowAtlasResult results[1];

        TEST_CHECK(ctx, Fluxion_ShadowAtlas_Allocate(&desc, requests, 1, results) == 1);
        TEST_CHECK(ctx, results[0].fitted && results[0].tileCount == 6);

        // Six DIFFERENT tiles. Six of the same one would light every
        // face of the cube with the same picture, which looks like a
        // shadow rather than being one.
        for (u32 a = 0; a < 6; ++a)
        {
            for (u32 b = a + 1; b < 6; ++b)
            {
                TEST_CHECK(ctx, results[0].tiles[a].x != results[0].tiles[b].x ||
                                results[0].tiles[a].y != results[0].tiles[b].y);
            }
        }
    }

    // --- The largest go first, the answers come back in the caller's order -
    //
    // Requested small-then-large; placed large-then-small. Without that
    // order the six-tile request would meet an atlas already full of
    // single tiles with no six-tile run left anywhere in it.
    {
        const FluxionShadowAtlasRequest requests[2] = { { 1, 100 }, { 6, 200 } };
        FluxionShadowAtlasResult results[2];

        TEST_CHECK(ctx, Fluxion_ShadowAtlas_Allocate(&desc, requests, 2, results) == 2);

        // Position in the array still means position in the request
        // list, whatever order they were placed in.
        TEST_CHECK(ctx, results[0].tag == 100 && results[1].tag == 200);

        // The point light took the first six tiles; the cascade got the
        // seventh.
        TEST_CHECK(ctx, results[1].tiles[0].x == 0 && results[1].tiles[0].y == 0);
        TEST_CHECK(ctx, results[0].tiles[0].x == 2 && results[0].tiles[0].y == 1);
    }

    // --- The same requests always land in the same places ------------------
    //
    // A frame that changed nothing must not move its shadows: a tile
    // that wandered would show up as the whole scene shimmering.
    {
        const FluxionShadowAtlasRequest requests[4] = { { 1, 1 }, { 6, 2 }, { 1, 3 }, { 6, 4 } };
        FluxionShadowAtlasResult first[4];
        FluxionShadowAtlasResult second[4];

        TEST_CHECK(ctx, Fluxion_ShadowAtlas_Allocate(&desc, requests, 4, first) == 4);
        TEST_CHECK(ctx, Fluxion_ShadowAtlas_Allocate(&desc, requests, 4, second) == 4);
        TEST_CHECK(ctx, memcmp(first, second, sizeof(first)) == 0);
    }

    // --- An atlas that is not a whole number of tiles is refused ----------
    //
    // Rounding would hand back tiles that fall off the edge, which is
    // not a smaller atlas -- it is one that reads memory belonging to
    // nothing.
    {
        FluxionShadowAtlasDesc ragged = desc;
        ragged.atlasSize = 4000;

        const FluxionShadowAtlasRequest requests[1] = { { 1, 0 } };
        FluxionShadowAtlasResult results[1];

        TEST_CHECK(ctx, Fluxion_ShadowAtlas_Allocate(&ragged, requests, 1, results) == 0);
        TEST_CHECK(ctx, !results[0].fitted);
    }

    // --- A tile as the part of the atlas it covers ------------------------
    {
        FluxionShadowAtlasTile origin = { 0, 0 };
        FluxionShadowAtlasTile opposite = { 3, 3 };

        TEST_CHECK(ctx, RectNear(Fluxion_ShadowAtlas_TileRect(&desc, origin), 0.0f, 0.0f, 0.25f));
        TEST_CHECK(ctx, RectNear(Fluxion_ShadowAtlas_TileRect(&desc, opposite), 0.75f, 0.75f, 0.25f));
    }
}
