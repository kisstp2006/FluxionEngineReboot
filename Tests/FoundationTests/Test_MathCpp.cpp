#include "TestFramework.h"

#include <Fluxion/Foundation/Math.hpp>

// main.c (plain C) calls this, so it needs unmangled C linkage.
extern "C" void Test_MathCpp_Run(TestContext* ctx)
{
    FluxionVec3 a{ 1.0f, 2.0f, 3.0f };
    FluxionVec3 b{ 4.0f, 5.0f, 6.0f };

    FluxionVec3 sum = a + b;
    TEST_CHECK(ctx, sum.x == 5.0f && sum.y == 7.0f && sum.z == 9.0f);

    FluxionVec3 diff = b - a;
    TEST_CHECK(ctx, diff.x == 3.0f && diff.y == 3.0f && diff.z == 3.0f);

    FluxionVec3 scaled = a * 2.0f;
    TEST_CHECK(ctx, scaled.x == 2.0f && scaled.y == 4.0f && scaled.z == 6.0f);

    FluxionVec3 scaledLeft = 2.0f * a;
    TEST_CHECK(ctx, scaledLeft.x == scaled.x && scaledLeft.y == scaled.y && scaledLeft.z == scaled.z);

    TEST_CHECK(ctx, Dot(a, b) == 32.0f); // 1*4 + 2*5 + 3*6

    FluxionVec3 crossResult = Cross(FluxionVec3{ 1.0f, 0.0f, 0.0f }, FluxionVec3{ 0.0f, 1.0f, 0.0f });
    TEST_CHECK(ctx, crossResult.x == 0.0f && crossResult.y == 0.0f && crossResult.z == 1.0f);

    FluxionVec3 unit{ 3.0f, 0.0f, 0.0f };
    TEST_CHECK(ctx, Length(unit) == 3.0f);
    FluxionVec3 normalized = Normalize(unit);
    TEST_CHECK(ctx, normalized.x == 1.0f && normalized.y == 0.0f && normalized.z == 0.0f);

    FluxionMat4 identity = Fluxion_Mat4_Identity();
    FluxionMat4 product = identity * identity;
    TEST_CHECK(ctx, product.m[0][0] == 1.0f && product.m[1][1] == 1.0f);

    FluxionQuat q = Fluxion_Quat_Identity();
    FluxionQuat qProduct = q * q;
    TEST_CHECK(ctx, qProduct.w == 1.0f);
}
