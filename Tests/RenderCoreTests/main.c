#include "TestFramework.h"

#include <Fluxion/Foundation/Log.h>

void Test_PassRegistry_Run(TestContext* ctx);
void Test_GraphCompile_Run(TestContext* ctx);
void Test_JsonLoader_Run(TestContext* ctx);
void Test_DumpDot_Run(TestContext* ctx);
void Test_ShaderProgram_Run(TestContext* ctx);
void Test_ShaderReload_Run(TestContext* ctx);
void Test_Material_Run(TestContext* ctx);
void Test_MeshBuffer_Run(TestContext* ctx);
void Test_MemoryDomains_Run(TestContext* ctx);
void Test_RenderView_Run(TestContext* ctx);
void Test_RendererFrame_Run(TestContext* ctx);
void Test_MeshAsset_Run(TestContext* ctx);
void Test_ShaderLibrary_Run(TestContext* ctx);
void Test_SurfaceData_Run(TestContext* ctx);

int main(void)
{
    TestContext ctx = { 0 };

    FLUXION_LOG_INFO("RenderCoreTests", "Running RenderCoreTests...");

    Test_PassRegistry_Run(&ctx);
    Test_GraphCompile_Run(&ctx);
    Test_JsonLoader_Run(&ctx);
    Test_DumpDot_Run(&ctx);
    Test_ShaderProgram_Run(&ctx);
    Test_ShaderReload_Run(&ctx);
    Test_Material_Run(&ctx);
    Test_MeshBuffer_Run(&ctx);
    Test_MemoryDomains_Run(&ctx);
    Test_RenderView_Run(&ctx);
    Test_RendererFrame_Run(&ctx);
    Test_MeshAsset_Run(&ctx);
    Test_ShaderLibrary_Run(&ctx);
    Test_SurfaceData_Run(&ctx);

    if (ctx.failures == 0)
    {
        FLUXION_LOG_INFO("RenderCoreTests", "All RenderCoreTests passed.");
        return 0;
    }

    FLUXION_LOG_ERROR("RenderCoreTests", "%d RenderCoreTests check(s) failed.", ctx.failures);
    return 1;
}
