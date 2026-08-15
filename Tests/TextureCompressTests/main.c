#include "TestFramework.h"

#include <Fluxion/Foundation/Log.h>

void Test_BlockCompress_Run(TestContext* ctx);

int main(void)
{
    TestContext ctx = { 0 };

    FLUXION_LOG_INFO("TextureCompressTests", "Running TextureCompressTests...");

    Test_BlockCompress_Run(&ctx);

    if (ctx.failures == 0)
    {
        FLUXION_LOG_INFO("TextureCompressTests", "All TextureCompressTests passed.");
        return 0;
    }

    FLUXION_LOG_ERROR("TextureCompressTests", "%d TextureCompressTests check(s) failed.", ctx.failures);
    return 1;
}
