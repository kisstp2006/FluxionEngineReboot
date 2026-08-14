#include "TestFramework.h"

#include <Fluxion/Foundation/Log.h>

void Test_Types_Run(TestContext* ctx);
void Test_Abi_Run(TestContext* ctx);
void Test_Assert_Run(TestContext* ctx);
void Test_Memory_Run(TestContext* ctx);
void Test_Containers_Run(TestContext* ctx);
void Test_Hashing_Run(TestContext* ctx);
void Test_Handle_Run(TestContext* ctx);
void Test_UUID_Run(TestContext* ctx);
void Test_Bit_Run(TestContext* ctx);
void Test_Atomic_Run(TestContext* ctx);
void Test_Math_Run(TestContext* ctx);
void Test_ResultCpp_Run(TestContext* ctx);
void Test_HandleCpp_Run(TestContext* ctx);
void Test_SpanStringViewCpp_Run(TestContext* ctx);
void Test_MathCpp_Run(TestContext* ctx);
void Test_FoundationUtilCpp_Run(TestContext* ctx);
void Test_MemoryTracker_Run(TestContext* ctx);
void Test_MemoryScopeCpp_Run(TestContext* ctx);
void Test_Stream_Run(TestContext* ctx);

int main(void)
{
    TestContext ctx = { 0 };

    FLUXION_LOG_INFO("FoundationTests", "Running FoundationTests...");

    Test_Types_Run(&ctx);
    Test_Abi_Run(&ctx);
    Test_Assert_Run(&ctx);
    Test_Memory_Run(&ctx);
    Test_Containers_Run(&ctx);
    Test_Hashing_Run(&ctx);
    Test_Handle_Run(&ctx);
    Test_UUID_Run(&ctx);
    Test_Bit_Run(&ctx);
    Test_Atomic_Run(&ctx);
    Test_Math_Run(&ctx);
    Test_ResultCpp_Run(&ctx);
    Test_HandleCpp_Run(&ctx);
    Test_SpanStringViewCpp_Run(&ctx);
    Test_MathCpp_Run(&ctx);
    Test_FoundationUtilCpp_Run(&ctx);
    Test_MemoryTracker_Run(&ctx);
    Test_MemoryScopeCpp_Run(&ctx);
    Test_Stream_Run(&ctx);

    if (ctx.failures == 0)
    {
        FLUXION_LOG_INFO("FoundationTests", "All FoundationTests passed.");
        return 0;
    }

    FLUXION_LOG_ERROR("FoundationTests", "%d FoundationTests check(s) failed.", ctx.failures);
    return 1;
}
