#include "TestFramework.h"

#include <cstdio>

void Test_Lexer_Run(TestContext& ctx);
void Test_Preprocessor_Run(TestContext& ctx);
void Test_Parser_Run(TestContext& ctx);
void Test_Semantic_Run(TestContext& ctx);
void Test_HLSLBackend_Run(TestContext& ctx);
void Test_UniformLayout_Run(TestContext& ctx);
void Test_HLSLBackend_EntryReturn_Run(TestContext& ctx);
void Test_GLSLBackend_Run(TestContext& ctx);
void Test_Discard_Run(TestContext& ctx);
void Test_DXCAdapter_Run(TestContext& ctx);
void Test_Compatibility_Run(TestContext& ctx);
void Test_ShaderCache_Run(TestContext& ctx);
void Test_BindingGroups_Run(TestContext& ctx);
void Test_BindingGroups_DefaultIsMaterial_Run(TestContext& ctx);
void Test_ComputeStorageBuffer_Run(TestContext& ctx);

int main()
{
    TestContext ctx;

    std::fprintf(stderr, "Running ShaderCompilerTests...\n");

    Test_Lexer_Run(ctx);
    Test_Preprocessor_Run(ctx);
    Test_Parser_Run(ctx);
    Test_Semantic_Run(ctx);
    Test_HLSLBackend_Run(ctx);
    Test_UniformLayout_Run(ctx);
    Test_HLSLBackend_EntryReturn_Run(ctx);
    Test_GLSLBackend_Run(ctx);
    Test_Discard_Run(ctx);
    Test_DXCAdapter_Run(ctx);
    Test_Compatibility_Run(ctx);
    Test_ShaderCache_Run(ctx);
    Test_BindingGroups_Run(ctx);
    Test_BindingGroups_DefaultIsMaterial_Run(ctx);
    Test_ComputeStorageBuffer_Run(ctx);

    if (ctx.failures == 0)
    {
        std::fprintf(stderr, "All ShaderCompilerTests passed.\n");
        return 0;
    }

    std::fprintf(stderr, "%d ShaderCompilerTests check(s) failed.\n", ctx.failures);
    return 1;
}
