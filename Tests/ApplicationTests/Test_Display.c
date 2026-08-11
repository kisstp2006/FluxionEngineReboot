#include "TestFramework.h"

#include <Fluxion/Application/Window/Display.h>

void Test_Display_Run(TestContext* ctx)
{
    u32 count = Fluxion_Display_GetCount();
    TEST_CHECK(ctx, count >= 1);

    FluxionDisplayInfo info;
    TEST_CHECK(ctx, Fluxion_Display_GetInfo(0, &info));
    TEST_CHECK(ctx, info.width > 0);
    TEST_CHECK(ctx, info.height > 0);
    TEST_CHECK(ctx, info.dpiScale > 0.0f);

    TEST_CHECK(ctx, Fluxion_Display_GetInfo(count, &info) == false); // out of range
}
