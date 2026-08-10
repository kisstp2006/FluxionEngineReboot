#include "TestFramework.h"

#include <Fluxion/Foundation/Log.h>

void Test_Types_Run(TestContext* ctx);
void Test_Assert_Run(TestContext* ctx);
void Test_Memory_Run(TestContext* ctx);
void Test_Containers_Run(TestContext* ctx);
void Test_Hashing_Run(TestContext* ctx);
void Test_Handle_Run(TestContext* ctx);
void Test_UUID_Run(TestContext* ctx);
void Test_Bit_Run(TestContext* ctx);
void Test_Atomic_Run(TestContext* ctx);
void Test_Math_Run(TestContext* ctx);

int main(void)
{
    TestContext ctx = { 0 };

    FLUXION_LOG_INFO("FoundationTests", "Running FoundationTests...");

    Test_Types_Run(&ctx);
    Test_Assert_Run(&ctx);
    Test_Memory_Run(&ctx);
    Test_Containers_Run(&ctx);
    Test_Hashing_Run(&ctx);
    Test_Handle_Run(&ctx);
    Test_UUID_Run(&ctx);
    Test_Bit_Run(&ctx);
    Test_Atomic_Run(&ctx);
    Test_Math_Run(&ctx);

    if (ctx.failures == 0)
    {
        FLUXION_LOG_INFO("FoundationTests", "All FoundationTests passed.");
        return 0;
    }

    FLUXION_LOG_ERROR("FoundationTests", "%d FoundationTests check(s) failed.", ctx.failures);
    return 1;
}
