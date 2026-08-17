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

#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FluxionVec2 { f32 x, y; } FluxionVec2;
typedef struct FluxionVec3 { f32 x, y, z; } FluxionVec3;
typedef struct FluxionVec4 { f32 x, y, z, w; } FluxionVec4;
typedef struct FluxionQuat { f32 x, y, z, w; } FluxionQuat;

// Row-major 4x4 matrix: m[row][col]. View/projection helpers (lookAt,
// perspective, ...) are intentionally not here — those belong with a
// future renderer/RHI layer, not Foundation.
typedef struct FluxionMat4 { f32 m[4][4]; } FluxionMat4;

static inline FluxionVec3 Fluxion_Vec3_Add(FluxionVec3 a, FluxionVec3 b)
{
    FluxionVec3 r = { a.x + b.x, a.y + b.y, a.z + b.z };
    return r;
}

static inline FluxionVec3 Fluxion_Vec3_Sub(FluxionVec3 a, FluxionVec3 b)
{
    FluxionVec3 r = { a.x - b.x, a.y - b.y, a.z - b.z };
    return r;
}

static inline FluxionVec3 Fluxion_Vec3_Scale(FluxionVec3 a, f32 s)
{
    FluxionVec3 r = { a.x * s, a.y * s, a.z * s };
    return r;
}

static inline f32 Fluxion_Vec3_Dot(FluxionVec3 a, FluxionVec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline FluxionVec3 Fluxion_Vec3_Cross(FluxionVec3 a, FluxionVec3 b)
{
    FluxionVec3 r = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    return r;
}

static inline f32 Fluxion_Vec3_Length(FluxionVec3 a)
{
    return sqrtf(Fluxion_Vec3_Dot(a, a));
}

static inline FluxionVec3 Fluxion_Vec3_Normalize(FluxionVec3 a)
{
    f32 length = Fluxion_Vec3_Length(a);
    if (length <= 0.00001f)
    {
        FluxionVec3 zero = { 0.0f, 0.0f, 0.0f };
        return zero;
    }
    return Fluxion_Vec3_Scale(a, 1.0f / length);
}

static inline FluxionMat4 Fluxion_Mat4_Identity(void)
{
    FluxionMat4 result;
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            result.m[row][col] = (row == col) ? 1.0f : 0.0f;
        }
    }
    return result;
}

static inline FluxionMat4 Fluxion_Mat4_Multiply(FluxionMat4 a, FluxionMat4 b)
{
    FluxionMat4 result;
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            f32 sum = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                sum += a.m[row][k] * b.m[k][col];
            }
            result.m[row][col] = sum;
        }
    }
    return result;
}

// The inverse of a matrix that only rotates and moves.
//
// Restricted on purpose. The general inverse of a four-by-four matrix is
// a much larger piece of arithmetic, and nothing here needs it: a view
// matrix is a rotation and a translation, and for those the inverse is
// the transpose of the rotation and that transpose applied to the
// negated translation. A matrix with scale, shear or perspective in it is
// NOT one of these, and this returns nonsense for one -- which is why it
// says so in its name rather than pretending to be an inverse.
static inline FluxionMat4 Fluxion_Mat4_RigidInverse(FluxionMat4 m)
{
    FluxionMat4 result = Fluxion_Mat4_Identity();

    for (int row = 0; row < 3; ++row)
    {
        for (int col = 0; col < 3; ++col)
        {
            result.m[row][col] = m.m[col][row];
        }
    }

    for (int row = 0; row < 3; ++row)
    {
        result.m[row][3] = -(m.m[0][row] * m.m[0][3] + m.m[1][row] * m.m[1][3] + m.m[2][row] * m.m[2][3]);
    }

    return result;
}

// The determinant of the three-by-three left when one row and one column
// are struck out. Only ever wanted by the general inverse below.
static inline f32 Fluxion_Mat4_Minor(FluxionMat4 m, int skipRow, int skipCol)
{
    f32 sub[3][3];
    int r = 0;

    for (int row = 0; row < 4; ++row)
    {
        if (row == skipRow) continue;
        int c = 0;
        for (int col = 0; col < 4; ++col)
        {
            if (col == skipCol) continue;
            sub[r][c] = m.m[row][col];
            ++c;
        }
        ++r;
    }

    return sub[0][0] * (sub[1][1] * sub[2][2] - sub[1][2] * sub[2][1])
         - sub[0][1] * (sub[1][0] * sub[2][2] - sub[1][2] * sub[2][0])
         + sub[0][2] * (sub[1][0] * sub[2][1] - sub[1][1] * sub[2][0]);
}

// The inverse of ANY four-by-four matrix -- the cheaper restricted one
// above cannot invert a projection, and undoing the projection is the
// whole of drawing a sky.
//
// Cofactors in a loop rather than one unrolled expression: the unrolled
// form is a wall of signed terms where one wrong index gives a subtly
// bent sky nobody would blame on arithmetic, and this runs once a frame.
// A singular matrix comes back as identity, not infinities -- obviously
// wrong beats quietly poisonous.
static inline FluxionMat4 Fluxion_Mat4_Inverse(FluxionMat4 m)
{
    f32 cofactors[4][4];
    f32 determinant = 0.0f;

    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            const f32 minor = Fluxion_Mat4_Minor(m, row, col);
            cofactors[row][col] = ((row + col) % 2 == 0) ? minor : -minor;
        }
    }

    for (int col = 0; col < 4; ++col) determinant += m.m[0][col] * cofactors[0][col];

    if (determinant > -1e-12f && determinant < 1e-12f) return Fluxion_Mat4_Identity();

    const f32 inverseDeterminant = 1.0f / determinant;

    // Transposed on the way out: the adjugate is the transpose of the
    // cofactor matrix, and doing it here rather than in a second pass is
    // the whole of the difference between an inverse and its transpose.
    FluxionMat4 result;
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            result.m[row][col] = cofactors[col][row] * inverseDeterminant;
        }
    }

    return result;
}

static inline FluxionMat4 Fluxion_Mat4_Translation(FluxionVec3 t)
{
    FluxionMat4 result = Fluxion_Mat4_Identity();
    result.m[0][3] = t.x;
    result.m[1][3] = t.y;
    result.m[2][3] = t.z;
    return result;
}

static inline FluxionQuat Fluxion_Quat_Identity(void)
{
    FluxionQuat q = { 0.0f, 0.0f, 0.0f, 1.0f };
    return q;
}

static inline FluxionQuat Fluxion_Quat_Multiply(FluxionQuat a, FluxionQuat b)
{
    FluxionQuat r;
    r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    r.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    r.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    return r;
}

static inline FluxionMat4 Fluxion_Mat4_Transposed(FluxionMat4 m)
{
    FluxionMat4 r;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            r.m[row][column] = m.m[column][row];
    return r;
}

// The six planes a view-projection clips against.
//
// Each is xyz the outward-facing... inward-facing normal and w the
// distance, so a point is inside when dot(normal, point) + w >= 0 for
// all six. They come out of the matrix by adding or subtracting two of
// its rows -- Gribb and Hartmann's derivation, which falls out of
// writing the clip-space test in terms of the untransformed point.
//
// Near is the third row ALONE rather than the sum of two, because depth
// comes out of every matrix in this engine in 0..1 rather than -1..1.
typedef struct FluxionFrustumPlanes
{
    FluxionVec4 planes[6];
} FluxionFrustumPlanes;

static inline FluxionVec4 Fluxion_FrustumInternal_Normalize(FluxionVec4 plane)
{
    const f32 length = sqrtf(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
    if (length <= 0.0f) return plane;

    const f32 inverse = 1.0f / length;
    FluxionVec4 r;
    r.x = plane.x * inverse;
    r.y = plane.y * inverse;
    r.z = plane.z * inverse;
    r.w = plane.w * inverse;
    return r;
}

static inline FluxionFrustumPlanes Fluxion_Mat4_FrustumPlanes(FluxionMat4 viewProjection)
{
    const f32* row0 = viewProjection.m[0];
    const f32* row1 = viewProjection.m[1];
    const f32* row2 = viewProjection.m[2];
    const f32* row3 = viewProjection.m[3];

    FluxionFrustumPlanes result;
    for (int i = 0; i < 4; ++i)
    {
        ((f32*)&result.planes[0])[i] = row3[i] + row0[i]; // left
        ((f32*)&result.planes[1])[i] = row3[i] - row0[i]; // right
        ((f32*)&result.planes[2])[i] = row3[i] + row1[i]; // bottom
        ((f32*)&result.planes[3])[i] = row3[i] - row1[i]; // top
        ((f32*)&result.planes[4])[i] = row2[i];           // near
        ((f32*)&result.planes[5])[i] = row3[i] - row2[i]; // far
    }

    // Normalized so the distance below is in world units rather than in
    // whatever scale the matrix happened to carry -- which is what lets
    // a radius be compared against it.
    for (int i = 0; i < 6; ++i) result.planes[i] = Fluxion_FrustumInternal_Normalize(result.planes[i]);
    return result;
}

// False when the sphere is wholly outside at least one plane.
//
// True does NOT mean it is inside: a sphere straddling two planes near a
// corner passes both tests and is still outside the frustum. That is the
// right way round for culling -- what is thrown away is only what
// certainly cannot be seen.
static inline bool Fluxion_Frustum_TouchesSphere(const FluxionFrustumPlanes* frustum, FluxionVec3 centre, f32 radius)
{
    if (frustum == NULL) return true;

    for (int i = 0; i < 6; ++i)
    {
        const FluxionVec4 plane = frustum->planes[i];
        const f32 distance = plane.x * centre.x + plane.y * centre.y + plane.z * centre.z + plane.w;
        if (distance < -radius) return false;
    }
    return true;
}

// Where a view matrix puts the eye, and which way it looks.
//
// A view matrix moves the world rather than the eye, so neither is in it
// to read off -- both come from undoing it. Written down once here
// because more than one caller wants them and each would otherwise index
// the same four numbers by hand, in a convention that is easy to get
// backwards and produces a picture rather than an error when it is.
//
// `forward` is the way the camera looks: negative Z in its own space,
// the same axis every object in this engine faces along.
static inline void Fluxion_Mat4_DecomposeView(FluxionMat4 view, FluxionVec3* outPosition, FluxionVec3* outForward)
{
    const FluxionMat4 cameraToWorld = Fluxion_Mat4_Inverse(view);

    if (outPosition != NULL)
    {
        outPosition->x = cameraToWorld.m[0][3];
        outPosition->y = cameraToWorld.m[1][3];
        outPosition->z = cameraToWorld.m[2][3];
    }

    if (outForward != NULL)
    {
        outForward->x = -cameraToWorld.m[0][2];
        outForward->y = -cameraToWorld.m[1][2];
        outForward->z = -cameraToWorld.m[2][2];
    }
}

// The rotation that turns an object's forward axis -- negative Z, the
// same one a camera looks down -- to point along `target`. Facing exactly
// backwards has no single answer, so one axis is picked rather than left
// to a cross product that comes out zero.
static inline FluxionQuat Fluxion_Quat_LookRotation(FluxionVec3 target)
{
    const FluxionVec3 forward = { 0.0f, 0.0f, -1.0f };
    const FluxionVec3 to = Fluxion_Vec3_Normalize(target);

    const f32 dot = forward.x * to.x + forward.y * to.y + forward.z * to.z;

    if (dot < -0.9999f)
    {
        FluxionQuat around = { 0.0f, 1.0f, 0.0f, 0.0f };
        return around;
    }
    if (dot > 0.9999f) return Fluxion_Quat_Identity();

    const FluxionVec3 axis = Fluxion_Vec3_Cross(forward, to);
    const f32 s = sqrtf((1.0f + dot) * 2.0f);

    FluxionQuat q;
    q.x = axis.x / s;
    q.y = axis.y / s;
    q.z = axis.z / s;
    q.w = s * 0.5f;
    return q;
}

#ifdef __cplusplus
}
#endif
