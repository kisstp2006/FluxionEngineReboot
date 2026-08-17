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

// Where each kind of light looks from. See the header for what each one
// is fitted to and why; maths from published descriptions, recorded in
// SHADER_SOURCES.md.

#include <Fluxion/RenderCore/Renderer/ShadowMatrices.h>

#include <math.h>
#include <string.h>

// An axis to build a frame around, chosen so it is never parallel to the
// direction it is crossed with. Up, unless the light points straight up
// or down, in which case any other axis does.
static FluxionVec3 Fluxion_ShadowMatricesInternal_FrameUp(FluxionVec3 forward)
{
    const FluxionVec3 up = { 0.0f, 1.0f, 0.0f };
    const FluxionVec3 sideways = { 0.0f, 0.0f, 1.0f };
    return (fabsf(forward.y) > 0.999f) ? sideways : up;
}

// The world seen from `eye`, looking along `forward`.
//
// Rows rather than columns, because that is what this engine's matrices
// are: the basis vectors read across, and the last column holds where
// the eye was moved to the origin.
static FluxionMat4 Fluxion_ShadowMatricesInternal_LookAlong(FluxionVec3 eye, FluxionVec3 forward)
{
    // The third axis points BACK along the view, because a camera here
    // looks down its own negative Z -- the same convention the scene
    // camera uses, and the reason a shadow matrix built the other way
    // round would put every shadow behind its caster.
    const FluxionVec3 zAxis = Fluxion_Vec3_Normalize(Fluxion_Vec3_Scale(forward, -1.0f));
    const FluxionVec3 xAxis = Fluxion_Vec3_Normalize(Fluxion_Vec3_Cross(Fluxion_ShadowMatricesInternal_FrameUp(zAxis), zAxis));
    const FluxionVec3 yAxis = Fluxion_Vec3_Cross(zAxis, xAxis);

    FluxionMat4 view = Fluxion_Mat4_Identity();
    view.m[0][0] = xAxis.x; view.m[0][1] = xAxis.y; view.m[0][2] = xAxis.z; view.m[0][3] = -Fluxion_Vec3_Dot(xAxis, eye);
    view.m[1][0] = yAxis.x; view.m[1][1] = yAxis.y; view.m[1][2] = yAxis.z; view.m[1][3] = -Fluxion_Vec3_Dot(yAxis, eye);
    view.m[2][0] = zAxis.x; view.m[2][1] = zAxis.y; view.m[2][2] = zAxis.z; view.m[2][3] = -Fluxion_Vec3_Dot(zAxis, eye);
    return view;
}

// A box, with no perspective in it. Depth to 0..1, the same range every
// other projection here produces.
static FluxionMat4 Fluxion_ShadowMatricesInternal_Orthographic(f32 halfWidth, f32 halfHeight, f32 nearPlane, f32 farPlane)
{
    FluxionMat4 projection;
    memset(&projection, 0, sizeof(projection));

    projection.m[0][0] = 1.0f / halfWidth;
    projection.m[1][1] = 1.0f / halfHeight;
    projection.m[2][2] = -1.0f / (farPlane - nearPlane);
    projection.m[2][3] = -nearPlane / (farPlane - nearPlane);
    projection.m[3][3] = 1.0f;
    return projection;
}

// The same perspective the scene camera builds, written out again here
// rather than shared: that one belongs to a camera component, and a
// light is not a camera. The numbers are the standard ones.
static FluxionMat4 Fluxion_ShadowMatricesInternal_Perspective(f32 fovYRadians, f32 nearPlane, f32 farPlane)
{
    FluxionMat4 projection;
    memset(&projection, 0, sizeof(projection));

    const f32 focal = 1.0f / tanf(fovYRadians * 0.5f);
    projection.m[0][0] = focal; // square, so the aspect is one
    projection.m[1][1] = focal;
    projection.m[2][2] = farPlane / (nearPlane - farPlane);
    projection.m[2][3] = (farPlane * nearPlane) / (nearPlane - farPlane);
    projection.m[3][2] = -1.0f;
    return projection;
}

FluxionMat4 Fluxion_ShadowMatrices_Directional(FluxionVec3 direction, FluxionVec3 centre, f32 radius, u32 tileSize)
{
    if (radius <= 0.0f) radius = 1.0f;

    const FluxionVec3 travel = Fluxion_Vec3_Normalize(direction);

    // Far enough back that the whole sphere is in front of the light,
    // and the slab runs from there to the far side of it. The sun has no
    // position, so this one is invented -- only the direction and the
    // extent matter, and both survive the choice.
    const FluxionVec3 eye = Fluxion_Vec3_Sub(centre, Fluxion_Vec3_Scale(travel, radius));
    FluxionMat4 view = Fluxion_ShadowMatricesInternal_LookAlong(eye, travel);

    // The whole point of the sphere: whatever the camera is doing, the
    // slab is the same size, so a texel covers the same amount of world
    // from one frame to the next.
    //
    // What is left is the slab SLIDING. The centre moves with the
    // camera, continuously, while the samples move in whole texels -- so
    // an edge crawls along itself as the camera walks. Rounding the
    // centre, in light space, to whole texels is what stops it: the map
    // then jumps by exactly the amount the samples do.
    if (tileSize != 0)
    {
        const f32 texelsPerUnit = (f32)tileSize / (2.0f * radius);

        const f32 centreX = view.m[0][0] * centre.x + view.m[0][1] * centre.y + view.m[0][2] * centre.z + view.m[0][3];
        const f32 centreY = view.m[1][0] * centre.x + view.m[1][1] * centre.y + view.m[1][2] * centre.z + view.m[1][3];

        const f32 snappedX = floorf(centreX * texelsPerUnit) / texelsPerUnit;
        const f32 snappedY = floorf(centreY * texelsPerUnit) / texelsPerUnit;

        // Applied to the view's own translation, which is where a shift
        // along the light's axes belongs -- moving the eye in world space
        // would need the frame built a second time to say the same thing.
        view.m[0][3] += snappedX - centreX;
        view.m[1][3] += snappedY - centreY;
    }

    // Zero to twice the radius: the eye sits one radius back, so the
    // sphere occupies exactly the middle of that. Nothing is clipped and
    // nothing is wasted.
    const FluxionMat4 projection = Fluxion_ShadowMatricesInternal_Orthographic(radius, radius, 0.0f, 2.0f * radius);
    return Fluxion_Mat4_Multiply(projection, view);
}

bool Fluxion_ShadowMatrices_CascadeSplits(f32 nearPlane, f32 farPlane, u32 count, f32 blend, f32* outSplits)
{
    if (outSplits == NULL) return false;
    if (count == 0 || count > FLUXION_SHADOW_MAX_CASCADES) return false;
    if (!(nearPlane > 0.0f) || !(farPlane > nearPlane)) return false;

    if (blend < 0.0f) blend = 0.0f;
    if (blend > 1.0f) blend = 1.0f;

    outSplits[0] = nearPlane;
    outSplits[count] = farPlane;

    const f32 range = farPlane - nearPlane;
    const f32 ratio = farPlane / nearPlane;

    for (u32 i = 1; i < count; ++i)
    {
        const f32 fraction = (f32)i / (f32)count;

        // Even shares of DISTANCE, and even shares of PERCEIVED detail.
        // Each is wrong alone -- see the header -- so the answer is a
        // point between them.
        const f32 uniform = nearPlane + range * fraction;
        const f32 logarithmic = nearPlane * powf(ratio, fraction);

        outSplits[i] = logarithmic * blend + uniform * (1.0f - blend);
    }

    return true;
}

FluxionMat4 Fluxion_ShadowMatrices_Spot(FluxionVec3 position, FluxionVec3 direction, f32 outerConeAngle, f32 range)
{
    if (range <= 0.0f) range = 1.0f;

    // A cone with no width sees nothing, and one at or past a half turn
    // is not a cone a single projection can hold.
    const f32 halfAngle = (outerConeAngle > 0.0001f && outerConeAngle < 1.5707f) ? outerConeAngle : 0.7853f;

    // Near enough to be out of the way, far enough that the depth
    // precision is not spent on the first few centimetres.
    const f32 nearPlane = range * 0.01f;

    const FluxionMat4 view = Fluxion_ShadowMatricesInternal_LookAlong(position, Fluxion_Vec3_Normalize(direction));

    // Doubled: the cone angle is measured from the axis, a field of view
    // spans the whole way across.
    const FluxionMat4 projection = Fluxion_ShadowMatricesInternal_Perspective(halfAngle * 2.0f, nearPlane, range);
    return Fluxion_Mat4_Multiply(projection, view);
}

FluxionMat4 Fluxion_ShadowMatrices_PointFace(FluxionVec3 position, u32 face, f32 range)
{
    if (range <= 0.0f) range = 1.0f;

    // The same six-face order as everything else that builds or reads a
    // cube here: +X, -X, +Y, -Y, +Z, -Z.
    static const FluxionVec3 kFaceDirections[6] = {
        {  1.0f,  0.0f,  0.0f },
        { -1.0f,  0.0f,  0.0f },
        {  0.0f,  1.0f,  0.0f },
        {  0.0f, -1.0f,  0.0f },
        {  0.0f,  0.0f,  1.0f },
        {  0.0f,  0.0f, -1.0f },
    };

    if (face > 5) face = 0;

    const f32 nearPlane = range * 0.01f;
    const FluxionMat4 view = Fluxion_ShadowMatricesInternal_LookAlong(position, kFaceDirections[face]);

    // A quarter turn each way. Six of those meet exactly, with no gap
    // and no overlap, which is what makes six enough.
    const FluxionMat4 projection = Fluxion_ShadowMatricesInternal_Perspective(1.5707963f, nearPlane, range);
    return Fluxion_Mat4_Multiply(projection, view);
}
