#include "TestFramework.h"

#include <Fluxion/Foundation/Types.h>

void Test_Types_Run(TestContext* ctx)
{
    TEST_CHECK(ctx, sizeof(u8) == 1);
    TEST_CHECK(ctx, sizeof(u16) == 2);
    TEST_CHECK(ctx, sizeof(u32) == 4);
    TEST_CHECK(ctx, sizeof(u64) == 8);
    TEST_CHECK(ctx, sizeof(i8) == 1);
    TEST_CHECK(ctx, sizeof(i16) == 2);
    TEST_CHECK(ctx, sizeof(i32) == 4);
    TEST_CHECK(ctx, sizeof(i64) == 8);
    TEST_CHECK(ctx, sizeof(f32) == 4);
    TEST_CHECK(ctx, sizeof(f64) == 8);

    u32 unsignedValue = 4000000000u;
    TEST_CHECK(ctx, unsignedValue > 0);

    i32 negativeValue = -1;
    TEST_CHECK(ctx, negativeValue < 0);
}
