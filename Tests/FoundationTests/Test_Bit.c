#include "TestFramework.h"

#include <Fluxion/Foundation/Bit.h>

void Test_Bit_Run(TestContext* ctx)
{
    TEST_CHECK(ctx, Fluxion_PopCount32(0xFFu) == 8);
    TEST_CHECK(ctx, Fluxion_PopCount32(0u) == 0);
    TEST_CHECK(ctx, Fluxion_PopCount64(0xFFFFFFFFFFFFFFFFull) == 64);

    TEST_CHECK(ctx, Fluxion_CountLeadingZeros32(1u) == 31);
    TEST_CHECK(ctx, Fluxion_CountLeadingZeros32(0u) == 32);
    TEST_CHECK(ctx, Fluxion_CountTrailingZeros32(8u) == 3);
    TEST_CHECK(ctx, Fluxion_CountTrailingZeros32(0u) == 32);

    TEST_CHECK(ctx, Fluxion_RotateLeft32(0x80000000u, 1u) == 1u);
    TEST_CHECK(ctx, Fluxion_RotateRight32(1u, 1u) == 0x80000000u);
    TEST_CHECK(ctx, Fluxion_RotateLeft32(0x12345678u, 0u) == 0x12345678u);

    TEST_CHECK(ctx, Fluxion_NextPowerOfTwo32(1u) == 1u);
    TEST_CHECK(ctx, Fluxion_NextPowerOfTwo32(5u) == 8u);
    TEST_CHECK(ctx, Fluxion_NextPowerOfTwo32(8u) == 8u);
    TEST_CHECK(ctx, Fluxion_NextPowerOfTwo32(0u) == 1u);
}
