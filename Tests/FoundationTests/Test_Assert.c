#include "TestFramework.h"

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Result.h>

void Test_Assert_Run(TestContext* ctx)
{
    // Only the passing path is exercised here — a failing FLUXION_ASSERT
    // triggers a debug break, which would kill the test process.
    FLUXION_ASSERT(1 == 1);
    FLUXION_ASSERT_MSG(2 + 2 == 4, "basic arithmetic must hold");
    FLUXION_VERIFY(1 + 1 == 2);

    FluxionResult ok = Fluxion_ResultOk();
    TEST_CHECK(ctx, ok.ok == true);
    TEST_CHECK(ctx, ok.code == 0);
    TEST_CHECK(ctx, ok.message == NULL);

    FluxionResult err = Fluxion_ResultError(42, "boom");
    TEST_CHECK(ctx, err.ok == false);
    TEST_CHECK(ctx, err.code == 42);
}
