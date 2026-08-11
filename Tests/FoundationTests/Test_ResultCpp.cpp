#include "TestFramework.h"

#include <Fluxion/Foundation/Result.hpp>
#include <Fluxion/Foundation/Types.h>

using Fluxion::Foundation::Result;

// main.c (plain C) calls this, so it needs unmangled C linkage.
extern "C" void Test_ResultCpp_Run(TestContext* ctx)
{
    Result<i32> ok = Result<i32>::Ok(42);
    TEST_CHECK(ctx, ok.IsOk());
    TEST_CHECK(ctx, ok.Value() == 42);
    TEST_CHECK(ctx, ok.Status().ok);

    Result<i32> err = Result<i32>::Error(7, "failed");
    TEST_CHECK(ctx, !err.IsOk());
    TEST_CHECK(ctx, err.Status().code == 7);

    Result<void> okVoid = Result<void>::Ok();
    TEST_CHECK(ctx, okVoid.IsOk());

    Result<void> errVoid = Result<void>::Error(3, "bad");
    TEST_CHECK(ctx, !errVoid.IsOk());
    TEST_CHECK(ctx, errVoid.Status().code == 3);
}
