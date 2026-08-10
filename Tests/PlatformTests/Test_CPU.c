#include "TestFramework.h"

#include <Fluxion/Platform/CPU.h>

void Test_CPU_Run(TestContext* ctx)
{
    u32 count = Fluxion_Platform_GetLogicalProcessorCount();
    TEST_CHECK(ctx, count >= 1);

    usize cacheLine = Fluxion_Platform_GetCacheLineSize();
    TEST_CHECK(ctx, cacheLine == 64);
}
