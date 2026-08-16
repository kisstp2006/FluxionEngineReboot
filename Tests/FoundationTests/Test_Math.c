#include "TestFramework.h"

#include <Fluxion/Foundation/Math.h>

static bool NearlyEqual(f32 a, f32 b)
{
    f32 diff = a - b;
    if (diff < 0.0f) diff = -diff;
    return diff < 0.0001f;
}


// An inverse is the matrix that undoes the first one, and that is exactly
// what is checked: the product has to be the identity. Comparing against
// a second, separately written inverse would only ask whether two pieces
// of arithmetic agree; this asks whether the arithmetic does what the
// word means.
static void TheInverseUndoesTheMatrix(TestContext* ctx, FluxionMat4 m)
{
    const FluxionMat4 inverse = Fluxion_Mat4_Inverse(m);
    const FluxionMat4 product = Fluxion_Mat4_Multiply(m, inverse);

    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            const f32 expected = (row == col) ? 1.0f : 0.0f;
            const f32 difference = product.m[row][col] - expected;
            TEST_CHECK(ctx, difference > -0.0005f && difference < 0.0005f);
        }
    }

    // And the other way round, because a matrix times its inverse coming
    // out as the identity is not on its own the same statement as the
    // inverse times the matrix -- for a wrongly transposed result it can
    // hold one way and not the other.
    const FluxionMat4 reversed = Fluxion_Mat4_Multiply(inverse, m);
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            const f32 expected = (row == col) ? 1.0f : 0.0f;
            const f32 difference = reversed.m[row][col] - expected;
            TEST_CHECK(ctx, difference > -0.0005f && difference < 0.0005f);
        }
    }
}

static void TheGeneralInverse(TestContext* ctx)
{
    TheInverseUndoesTheMatrix(ctx, Fluxion_Mat4_Identity());

    FluxionVec3 offset = { 3.0f, -4.0f, 5.0f };
    TheInverseUndoesTheMatrix(ctx, Fluxion_Mat4_Translation(offset));

    // A rotation and a translation together -- the case the restricted
    // inverse beside it already covers, checked here so the general one
    // is known to agree with it rather than merely to exist.
    FluxionMat4 rigid = Fluxion_Mat4_Translation(offset);
    rigid.m[0][0] = 0.0f; rigid.m[0][1] = -1.0f;
    rigid.m[1][0] = 1.0f; rigid.m[1][1] = 0.0f;
    TheInverseUndoesTheMatrix(ctx, rigid);

    const FluxionMat4 rigidInverse = Fluxion_Mat4_RigidInverse(rigid);
    const FluxionMat4 generalInverse = Fluxion_Mat4_Inverse(rigid);
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            const f32 difference = rigidInverse.m[row][col] - generalInverse.m[row][col];
            TEST_CHECK(ctx, difference > -0.0005f && difference < 0.0005f);
        }
    }

    // A perspective projection: the matrix the restricted inverse gets
    // WRONG, and the reason this one exists at all.
    FluxionMat4 projection = { { { 0.0f } } };
    const f32 fovScale = 1.7320508f; // one over the tangent of thirty degrees
    projection.m[0][0] = fovScale / 1.6f;
    projection.m[1][1] = fovScale;
    projection.m[2][2] = -(100.0f + 0.1f) / (100.0f - 0.1f);
    projection.m[2][3] = -(2.0f * 100.0f * 0.1f) / (100.0f - 0.1f);
    projection.m[3][2] = -1.0f;
    TheInverseUndoesTheMatrix(ctx, projection);

    // A scale, which the restricted one also gets wrong.
    FluxionMat4 scaled = Fluxion_Mat4_Identity();
    scaled.m[0][0] = 2.0f;
    scaled.m[1][1] = 0.5f;
    scaled.m[2][2] = -3.0f;
    TheInverseUndoesTheMatrix(ctx, scaled);

    // A matrix with no inverse comes back as the identity rather than as
    // infinities. A degenerate camera should give a picture that is
    // obviously wrong, not one full of values that poison whatever they
    // touch -- and a NaN spreads silently through every pixel it reaches.
    FluxionMat4 flattened = Fluxion_Mat4_Identity();
    flattened.m[2][2] = 0.0f;
    flattened.m[3][3] = 0.0f;
    const FluxionMat4 refused = Fluxion_Mat4_Inverse(flattened);
    TEST_CHECK(ctx, NearlyEqual(refused.m[0][0], 1.0f));
    TEST_CHECK(ctx, NearlyEqual(refused.m[1][2], 0.0f));
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

    TheGeneralInverse(ctx);
}
