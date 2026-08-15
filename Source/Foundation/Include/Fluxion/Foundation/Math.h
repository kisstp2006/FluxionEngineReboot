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

#ifdef __cplusplus
}
#endif
