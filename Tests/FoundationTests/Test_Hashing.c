#include "TestFramework.h"

#include <Fluxion/Foundation/Hashing.h>

void Test_Hashing_Run(TestContext* ctx)
{
    u32 h1 = Fluxion_HashString32("Fluxion");
    u32 h2 = Fluxion_HashString32("Fluxion");
    u32 h3 = Fluxion_HashString32("fluxion");
    TEST_CHECK(ctx, h1 == h2);
    TEST_CHECK(ctx, h1 != h3);

    u64 h64a = Fluxion_HashString64("Engine");
    u64 h64b = Fluxion_HashString64("Engine");
    TEST_CHECK(ctx, h64a == h64b);

    i32 a = 5;
    i32 b = 5;
    i32 c = 6;
    TEST_CHECK(ctx, Fluxion_BytesEqual(&a, &b, sizeof(i32)) == true);
    TEST_CHECK(ctx, Fluxion_BytesEqual(&a, &c, sizeof(i32)) == false);
}
