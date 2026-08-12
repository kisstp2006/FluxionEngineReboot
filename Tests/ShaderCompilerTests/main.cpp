#include "TestFramework.h"

#include <cstdio>

void Test_Lexer_Run(TestContext& ctx);
void Test_Parser_Run(TestContext& ctx);
void Test_Semantic_Run(TestContext& ctx);
void Test_HLSLBackend_Run(TestContext& ctx);
void Test_HLSLBackend_CSharpStyleReturn_Run(TestContext& ctx);
void Test_GLSLBackend_Run(TestContext& ctx);
void Test_DXCAdapter_Run(TestContext& ctx);
void Test_Compatibility_Run(TestContext& ctx);

int main()
{
    TestContext ctx;

    std::fprintf(stderr, "Running ShaderCompilerTests...\n");

    Test_Lexer_Run(ctx);
    Test_Parser_Run(ctx);
    Test_Semantic_Run(ctx);
    Test_HLSLBackend_Run(ctx);
    Test_HLSLBackend_CSharpStyleReturn_Run(ctx);
    Test_GLSLBackend_Run(ctx);
    Test_DXCAdapter_Run(ctx);
    Test_Compatibility_Run(ctx);

    if (ctx.failures == 0)
    {
        std::fprintf(stderr, "All ShaderCompilerTests passed.\n");
        return 0;
    }

    std::fprintf(stderr, "%d ShaderCompilerTests check(s) failed.\n", ctx.failures);
    return 1;
}
