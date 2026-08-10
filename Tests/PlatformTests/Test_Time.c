#include "TestFramework.h"

#include <Fluxion/Platform/Time.h>

void Test_Time_Run(TestContext* ctx)
{
    u64 frequency = Fluxion_Platform_GetHighResolutionFrequency();
    TEST_CHECK(ctx, frequency > 0);

    u64 start = Fluxion_Platform_GetHighResolutionTicks();
    Fluxion_Platform_SleepMilliseconds(20);
    u64 end = Fluxion_Platform_GetHighResolutionTicks();

    TEST_CHECK(ctx, end > start);

    f64 elapsedSeconds = Fluxion_Platform_TicksToSeconds(end - start, frequency);
    TEST_CHECK(ctx, elapsedSeconds >= 0.010); // allow scheduler slack, just prove the right ballpark
    TEST_CHECK(ctx, elapsedSeconds < 2.0);
}
