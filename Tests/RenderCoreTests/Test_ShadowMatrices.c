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

#include <Fluxion/RenderCore/Renderer/ShadowMatrices.h>

#include <math.h>

// What each light's matrix claims, checked against where it actually
// sends a point.
//
// There is no second implementation to compare against, so these are
// statements about the CONSTRUCTION: the centre lands in the middle, the
// edge lands on the edge, the near face lands at the near plane. A
// matrix that was transposed, or built around the wrong axis, or given a
// cone angle where it wanted a field of view, fails one of them by a
// long way rather than by a rounding error.

// Where a matrix sends a world point, after the divide that turns four
// numbers back into a place.
static FluxionVec3 Project(FluxionMat4 m, FluxionVec3 p)
{
    const f32 x = m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2] * p.z + m.m[0][3];
    const f32 y = m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2] * p.z + m.m[1][3];
    const f32 z = m.m[2][0] * p.x + m.m[2][1] * p.y + m.m[2][2] * p.z + m.m[2][3];
    const f32 w = m.m[3][0] * p.x + m.m[3][1] * p.y + m.m[3][2] * p.z + m.m[3][3];

    const f32 divisor = (w > 0.0001f || w < -0.0001f) ? w : 1.0f;
    FluxionVec3 result = { x / divisor, y / divisor, z / divisor };
    return result;
}

static bool Near(f32 value, f32 expected, f32 tolerance)
{
    const f32 difference = value - expected;
    return (difference < 0.0f ? -difference : difference) <= tolerance;
}

void Test_ShadowMatrices_Run(TestContext* ctx)
{
    // --- The sun: a sphere in, the whole of clip space out ---------------
    {
        const FluxionVec3 straightDown = { 0.0f, -1.0f, 0.0f };
        const FluxionVec3 centre = { 10.0f, 0.0f, -5.0f };
        const f32 radius = 20.0f;

        // No snapping, so the arithmetic is exact and the checks below
        // are about the fit rather than about the rounding.
        const FluxionMat4 sun = Fluxion_ShadowMatrices_Directional(straightDown, centre, radius, 0);

        // The middle of the sphere is the middle of the map.
        const FluxionVec3 middle = Project(sun, centre);
        TEST_CHECK(ctx, Near(middle.x, 0.0f, 0.001f) && Near(middle.y, 0.0f, 0.001f));

        // And halfway along the light's own axis, since the slab runs
        // from one radius before the centre to one radius after. Halfway
        // is 0.5 rather than 0: depth here runs 0..1, not -1..1.
        TEST_CHECK(ctx, Near(middle.z, 0.5f, 0.001f));

        // The near face of the slab -- one radius towards the light --
        // lands on the near plane, and the far face on the far one. A
        // matrix built around the wrong axis puts these the other way
        // round, which is a shadow cast by nothing.
        const FluxionVec3 towardsLight = { centre.x, centre.y + radius, centre.z };
        const FluxionVec3 awayFromLight = { centre.x, centre.y - radius, centre.z };
        TEST_CHECK(ctx, Near(Project(sun, towardsLight).z, 0.0f, 0.001f));
        TEST_CHECK(ctx, Near(Project(sun, awayFromLight).z, 1.0f, 0.001f));

        // A point one radius sideways lands on the edge. This is what
        // says the slab is exactly as wide as the sphere: too wide
        // wastes resolution, too narrow clips the shadow off.
        const FluxionVec3 sideways = { centre.x + radius, centre.y, centre.z };
        const FluxionVec3 edge = Project(sun, sideways);
        TEST_CHECK(ctx, Near(fabsf(edge.x), 1.0f, 0.001f) || Near(fabsf(edge.y), 1.0f, 0.001f));
    }

    // --- The sun again: snapping holds the map still ----------------------
    //
    // Two centres less than one texel apart must give the SAME matrix.
    // That is the whole point of the snapping: the map moves in the
    // steps the samples move in, so an edge stops crawling along itself
    // while the camera walks.
    {
        const FluxionVec3 direction = { 0.3f, -0.8f, 0.5f };
        const f32 radius = 32.0f;
        const u32 tileSize = 1024;

        // One texel covers 2 * radius / tileSize of world; a step of a
        // tenth of that cannot cross a boundary from every start, so the
        // check is that SOME nearby pair agrees rather than that this
        // particular one does.
        const f32 texelWorldSize = 2.0f * radius / (f32)tileSize;

        const FluxionVec3 first = { 0.0f, 0.0f, 0.0f };
        const FluxionVec3 nudged = { texelWorldSize * 0.01f, 0.0f, 0.0f };

        const FluxionMat4 a = Fluxion_ShadowMatrices_Directional(direction, first, radius, tileSize);
        const FluxionMat4 b = Fluxion_ShadowMatrices_Directional(direction, nudged, radius, tileSize);

        // A hundredth of a texel: whatever the starting phase, both land
        // in the same texel unless the first sat exactly on a boundary,
        // and this one does not.
        TEST_CHECK(ctx, Near(a.m[0][3], b.m[0][3], 0.0001f));
        TEST_CHECK(ctx, Near(a.m[1][3], b.m[1][3], 0.0001f));

        // And a whole texel away really does move it, or the snapping
        // would be a matrix that never follows the camera at all.
        const FluxionVec3 stepped = { texelWorldSize * 4.0f, 0.0f, 0.0f };
        const FluxionMat4 c = Fluxion_ShadowMatrices_Directional(direction, stepped, radius, tileSize);
        TEST_CHECK(ctx, !Near(a.m[0][3], c.m[0][3], 0.0001f) || !Near(a.m[1][3], c.m[1][3], 0.0001f));
    }

    // --- Cascade splits ---------------------------------------------------
    {
        f32 splits[FLUXION_SHADOW_MAX_CASCADES + 1];

        TEST_CHECK(ctx, Fluxion_ShadowMatrices_CascadeSplits(0.1f, 100.0f, 4, 0.8f, splits));

        // The ends are the range asked for, whatever the scheme does
        // between them.
        TEST_CHECK(ctx, Near(splits[0], 0.1f, 0.0001f));
        TEST_CHECK(ctx, Near(splits[4], 100.0f, 0.001f));

        // Every cascade covers something, and they go outwards.
        for (u32 i = 0; i < 4; ++i) TEST_CHECK(ctx, splits[i] < splits[i + 1]);

        // Blend zero is even shares of distance: the middle cut of four
        // sits exactly halfway along the range.
        f32 even[FLUXION_SHADOW_MAX_CASCADES + 1];
        TEST_CHECK(ctx, Fluxion_ShadowMatrices_CascadeSplits(0.0f + 1.0f, 101.0f, 4, 0.0f, even));
        TEST_CHECK(ctx, Near(even[2], 51.0f, 0.001f));

        // Blend one is even shares of perceived detail: each cut is the
        // same MULTIPLE of the one before, so the middle of four is the
        // geometric mean of the ends.
        f32 logarithmic[FLUXION_SHADOW_MAX_CASCADES + 1];
        TEST_CHECK(ctx, Fluxion_ShadowMatrices_CascadeSplits(1.0f, 100.0f, 2, 1.0f, logarithmic));
        TEST_CHECK(ctx, Near(logarithmic[1], 10.0f, 0.01f));

        // And a leaning-logarithmic split puts its near cut much closer
        // in than an even one would -- which is the reason for the
        // scheme, since that is where almost every pixel is.
        f32 leaning[FLUXION_SHADOW_MAX_CASCADES + 1];
        f32 plain[FLUXION_SHADOW_MAX_CASCADES + 1];
        TEST_CHECK(ctx, Fluxion_ShadowMatrices_CascadeSplits(0.1f, 500.0f, 4, 0.9f, leaning));
        TEST_CHECK(ctx, Fluxion_ShadowMatrices_CascadeSplits(0.1f, 500.0f, 4, 0.0f, plain));
        TEST_CHECK(ctx, leaning[1] < plain[1]);

        // Refused rather than guessed at.
        TEST_CHECK(ctx, !Fluxion_ShadowMatrices_CascadeSplits(0.1f, 100.0f, 0, 0.5f, splits));
        TEST_CHECK(ctx, !Fluxion_ShadowMatrices_CascadeSplits(0.1f, 100.0f, FLUXION_SHADOW_MAX_CASCADES + 1, 0.5f, splits));
        TEST_CHECK(ctx, !Fluxion_ShadowMatrices_CascadeSplits(100.0f, 0.1f, 4, 0.5f, splits));
        TEST_CHECK(ctx, !Fluxion_ShadowMatrices_CascadeSplits(0.0f, 100.0f, 4, 0.5f, splits));
    }

    // --- A spot light's cone ----------------------------------------------
    {
        const FluxionVec3 position = { 0.0f, 5.0f, 0.0f };
        const FluxionVec3 downwards = { 0.0f, -1.0f, 0.0f };
        const f32 outerCone = 0.5f; // radians from the axis
        const f32 range = 40.0f;

        const FluxionMat4 spot = Fluxion_ShadowMatrices_Spot(position, downwards, outerCone, range);

        // Straight down the axis is the middle of the map, at every
        // distance -- which is what says the projection is centred on
        // the light's own direction rather than on an axis of the world.
        const FluxionVec3 onAxisNear = { 0.0f, 4.0f, 0.0f };
        const FluxionVec3 onAxisFar = { 0.0f, -20.0f, 0.0f };
        TEST_CHECK(ctx, Near(Project(spot, onAxisNear).x, 0.0f, 0.001f));
        TEST_CHECK(ctx, Near(Project(spot, onAxisNear).y, 0.0f, 0.001f));
        TEST_CHECK(ctx, Near(Project(spot, onAxisFar).x, 0.0f, 0.001f));
        TEST_CHECK(ctx, Near(Project(spot, onAxisFar).y, 0.0f, 0.001f));

        // A point exactly on the cone's outer edge lands on the edge of
        // the map. THIS is the check that the angle was doubled: given
        // the half-angle where a field of view was wanted, the cone
        // would spill well outside its map and its shadow would be cut
        // off in a circle.
        const f32 distance = 10.0f;
        const FluxionVec3 onCone = { tanf(outerCone) * distance, 5.0f - distance, 0.0f };
        TEST_CHECK(ctx, Near(fabsf(Project(spot, onCone).x), 1.0f, 0.01f));

        // The far end of the light's reach is the far plane: a shadow
        // needs to go exactly as far as the light does.
        const FluxionVec3 atRange = { 0.0f, 5.0f - range, 0.0f };
        TEST_CHECK(ctx, Near(Project(spot, atRange).z, 1.0f, 0.01f));
    }

    // --- A point light's six faces ----------------------------------------
    {
        const FluxionVec3 position = { 1.0f, 2.0f, 3.0f };
        const f32 range = 25.0f;

        // Each face looks along its own axis: a point out that way lands
        // in the middle of that face and nowhere else.
        const FluxionVec3 alongFace[6] = {
            { 11.0f,  2.0f,  3.0f },
            { -9.0f,  2.0f,  3.0f },
            {  1.0f, 12.0f,  3.0f },
            {  1.0f, -8.0f,  3.0f },
            {  1.0f,  2.0f, 13.0f },
            {  1.0f,  2.0f, -7.0f },
        };

        for (u32 face = 0; face < 6; ++face)
        {
            const FluxionMat4 m = Fluxion_ShadowMatrices_PointFace(position, face, range);
            const FluxionVec3 centre = Project(m, alongFace[face]);
            TEST_CHECK(ctx, Near(centre.x, 0.0f, 0.001f) && Near(centre.y, 0.0f, 0.001f));
        }

        // Ninety degrees each way, which is what makes six enough: at a
        // given distance the face's edge is exactly that distance
        // sideways. Narrower leaves gaps between the faces; wider
        // overlaps and wastes the map.
        const FluxionMat4 positiveX = Fluxion_ShadowMatrices_PointFace(position, 0, range);
        const FluxionVec3 atCorner = { 1.0f + 10.0f, 2.0f + 10.0f, 3.0f };
        TEST_CHECK(ctx, Near(fabsf(Project(positiveX, atCorner).y), 1.0f, 0.01f));
    }

    // --- The sphere around a slice of what the camera sees --------------
    {
        const FluxionVec3 eye = { 5.0f, 1.0f, -2.0f };
        const FluxionVec3 forward = { 0.0f, 0.0f, -1.0f };
        const f32 fov = 1.0472f;
        const f32 aspect = 16.0f / 9.0f;
        const f32 sliceNear = 2.0f;
        const f32 sliceFar = 10.0f;

        f32 radius = 0.0f;
        const FluxionVec3 centre = Fluxion_ShadowMatrices_CascadeSphere(eye, forward, fov, aspect,
                                                                       sliceNear, sliceFar, &radius);

        // On the axis the camera looks down, and in front of it.
        TEST_CHECK(ctx, Near(centre.x, eye.x, 0.001f) && Near(centre.y, eye.y, 0.001f));
        TEST_CHECK(ctx, centre.z < eye.z);

        // Every corner of the slice is inside, and the far ones are ON
        // the surface -- a sphere any smaller would cut the slice, and
        // any larger would spend resolution on nothing.
        const f32 halfHeight = tanf(fov * 0.5f);
        for (u32 corner = 0; corner < 8; ++corner)
        {
            const f32 distance = (corner & 4u) != 0 ? sliceFar : sliceNear;
            const f32 x = ((corner & 1u) != 0 ? 1.0f : -1.0f) * distance * halfHeight * aspect;
            const f32 y = ((corner & 2u) != 0 ? 1.0f : -1.0f) * distance * halfHeight;

            const FluxionVec3 point = { eye.x + x, eye.y + y, eye.z - distance };
            const FluxionVec3 offset = { point.x - centre.x, point.y - centre.y, point.z - centre.z };
            const f32 length = sqrtf(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);

            TEST_CHECK(ctx, length <= radius + 0.001f);
            if ((corner & 4u) != 0) TEST_CHECK(ctx, Near(length, radius, 0.001f));
        }

        // A slice starting at the eye is a cone, and its sphere is the
        // one around the far quad alone -- the near "corners" are a
        // single point already inside it.
        f32 fromNothing = 0.0f;
        Fluxion_ShadowMatrices_CascadeSphere(eye, forward, fov, aspect, 0.0f, sliceFar, &fromNothing);
        TEST_CHECK(ctx, fromNothing > 0.0f && fromNothing <= radius * 2.0f);
    }

    // --- The two offsets that keep a surface from shadowing itself ------
    {
        f32 depthBias = 0.0f;
        f32 normalBias = 0.0f;
        Fluxion_ShadowMatrices_DirectionalBias(8.0f, 1024, &depthBias, &normalBias);
        TEST_CHECK(ctx, depthBias > 0.0f && normalBias > 0.0f);

        // Both are one texel's worth of something, so both halve when the
        // map doubles. That relationship is the whole content of them --
        // a bias that did not follow the resolution would be a number
        // that happened to work at one size.
        f32 finerDepth = 0.0f;
        f32 finerNormal = 0.0f;
        Fluxion_ShadowMatrices_DirectionalBias(8.0f, 2048, &finerDepth, &finerNormal);
        TEST_CHECK(ctx, Near(finerDepth, depthBias * 0.5f, 0.0001f));
        TEST_CHECK(ctx, Near(finerNormal, normalBias * 0.5f, 0.0001f));

        // A wider slab covers more world per texel, so what a lookup must
        // step out along the normal grows with it -- while the depth
        // offset, which is measured in the slab's own 0..1, does not.
        f32 widerDepth = 0.0f;
        f32 widerNormal = 0.0f;
        Fluxion_ShadowMatrices_DirectionalBias(16.0f, 1024, &widerDepth, &widerNormal);
        TEST_CHECK(ctx, Near(widerDepth, depthBias, 0.0001f));
        TEST_CHECK(ctx, Near(widerNormal, normalBias * 2.0f, 0.0001f));

        // Asking for one of them is allowed.
        Fluxion_ShadowMatrices_DirectionalBias(8.0f, 1024, NULL, &normalBias);
    }
}
