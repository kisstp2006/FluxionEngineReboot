#include "TestFramework.h"

#include <cstdio>

void Test_Lexer_Run(TestContext& ctx);
void Test_Parser_Run(TestContext& ctx);
void Test_Semantic_Run(TestContext& ctx);
void Test_Bytecode_Run(TestContext& ctx);
void Test_VmExecution_Run(TestContext& ctx);
void Test_VmExecution_Fixture_Run(TestContext& ctx);
void Test_Objects_Run(TestContext& ctx);
void Test_Structs_Run(TestContext& ctx);
void Test_Casts_Run(TestContext& ctx);
void Test_Enums_Run(TestContext& ctx);
void Test_Mathf_Run(TestContext& ctx);
void Test_Generics_Run(TestContext& ctx);
void Test_Arrays_Run(TestContext& ctx);
void Test_Gc_Run(TestContext& ctx);
void Test_Binding_Run(TestContext& ctx);
void Test_StackTrace_Run(TestContext& ctx);
void Test_Serialization_Run(TestContext& ctx);

int main()
{
    TestContext ctx;

    std::fprintf(stderr, "Running ScriptTests...\n");

    Test_Lexer_Run(ctx);
    Test_Parser_Run(ctx);
    Test_Semantic_Run(ctx);
    Test_Bytecode_Run(ctx);
    Test_VmExecution_Run(ctx);
    Test_VmExecution_Fixture_Run(ctx);
    Test_Objects_Run(ctx);
    Test_Structs_Run(ctx);
    Test_Casts_Run(ctx);
    Test_Enums_Run(ctx);
    Test_Mathf_Run(ctx);
    Test_Generics_Run(ctx);
    Test_Arrays_Run(ctx);
    Test_Gc_Run(ctx);
    Test_Binding_Run(ctx);
    Test_StackTrace_Run(ctx);
    Test_Serialization_Run(ctx);

    if (ctx.failures == 0)
    {
        std::fprintf(stderr, "All ScriptTests passed.\n");
        return 0;
    }

    std::fprintf(stderr, "%d ScriptTests check(s) failed.\n", ctx.failures);
    return 1;
}
