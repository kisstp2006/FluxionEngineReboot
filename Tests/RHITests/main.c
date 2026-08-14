#include "TestFramework.h"

#include <Fluxion/Foundation/Log.h>

void Test_Handles_Run(TestContext* ctx);
void Test_Capabilities_Run(TestContext* ctx);
void Test_NullBackend_Run(TestContext* ctx);
void Test_NativeHandle_Run(TestContext* ctx);
void Test_VulkanBackend_Run(TestContext* ctx);
void Test_OpenGLBackend_Run(TestContext* ctx);
void Test_BindGroup_Run(TestContext* ctx);
void Test_PipelineCacheFile_Run(TestContext* ctx);
#if defined(_WIN32)
// D3D12 doesn't exist outside Windows -- Test_D3D12Backend.c is only
// compiled into this executable on that platform (see CMakeLists.txt),
// so this declaration/call must stay behind the same guard.
void Test_D3D12Backend_Run(TestContext* ctx);
#endif

int main(void)
{
    TestContext ctx = { 0 };

    FLUXION_LOG_INFO("RHITests", "Running RHITests...");

    Test_Handles_Run(&ctx);
    Test_Capabilities_Run(&ctx);
    Test_NullBackend_Run(&ctx);
    Test_NativeHandle_Run(&ctx);
    Test_VulkanBackend_Run(&ctx);
    Test_OpenGLBackend_Run(&ctx);
    Test_BindGroup_Run(&ctx);
    Test_PipelineCacheFile_Run(&ctx);
#if defined(_WIN32)
    Test_D3D12Backend_Run(&ctx);
#endif

    if (ctx.failures == 0)
    {
        FLUXION_LOG_INFO("RHITests", "All RHITests passed.");
        return 0;
    }

    FLUXION_LOG_ERROR("RHITests", "%d RHITests check(s) failed.", ctx.failures);
    return 1;
}
