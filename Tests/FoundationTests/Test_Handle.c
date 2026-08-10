#include "TestFramework.h"

#include <Fluxion/Foundation/Handle.h>

FLUXION_DEFINE_HANDLE(TestAssetHandle);

void Test_Handle_Run(TestContext* ctx)
{
    TestAssetHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(invalid) == false);

    TestAssetHandle valid = { 3, 1 };
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(valid) == true);
    TEST_CHECK(ctx, valid.index == 3);
    TEST_CHECK(ctx, valid.generation == 1);
}
