#include "TestFramework.h"

#include <Fluxion/Foundation/Math.h>

static bool NearlyEqual(f32 a, f32 b)
{
    f32 diff = a - b;
    if (diff < 0.0f) diff = -diff;
    return diff < 0.0001f;
}

void Test_Math_Run(TestContext* ctx)
{
    FluxionVec3 a = { 1.0f, 2.0f, 3.0f };
    FluxionVec3 b = { 4.0f, 5.0f, 6.0f };

    FluxionVec3 sum = Fluxion_Vec3_Add(a, b);
    TEST_CHECK(ctx, NearlyEqual(sum.x, 5.0f) && NearlyEqual(sum.y, 7.0f) && NearlyEqual(sum.z, 9.0f));

    FluxionVec3 diff = Fluxion_Vec3_Sub(b, a);
    TEST_CHECK(ctx, NearlyEqual(diff.x, 3.0f) && NearlyEqual(diff.y, 3.0f) && NearlyEqual(diff.z, 3.0f));

    f32 dot = Fluxion_Vec3_Dot(a, b);
    TEST_CHECK(ctx, NearlyEqual(dot, 32.0f));

    FluxionVec3 unitX = { 1.0f, 0.0f, 0.0f };
    FluxionVec3 unitY = { 0.0f, 1.0f, 0.0f };
    FluxionVec3 cross = Fluxion_Vec3_Cross(unitX, unitY);
    TEST_CHECK(ctx, NearlyEqual(cross.z, 1.0f));

    FluxionVec3 nonUnit = { 3.0f, 0.0f, 0.0f };
    FluxionVec3 normalized = Fluxion_Vec3_Normalize(nonUnit);
    TEST_CHECK(ctx, NearlyEqual(Fluxion_Vec3_Length(normalized), 1.0f));

    FluxionMat4 identity = Fluxion_Mat4_Identity();
    FluxionMat4 translated = Fluxion_Mat4_Translation(a);
    FluxionMat4 product = Fluxion_Mat4_Multiply(identity, translated);
    TEST_CHECK(ctx, NearlyEqual(product.m[0][3], 1.0f));
    TEST_CHECK(ctx, NearlyEqual(product.m[1][3], 2.0f));
    TEST_CHECK(ctx, NearlyEqual(product.m[2][3], 3.0f));

    FluxionQuat q = Fluxion_Quat_Identity();
    FluxionQuat qq = Fluxion_Quat_Multiply(q, q);
    TEST_CHECK(ctx, NearlyEqual(qq.w, 1.0f));
}
