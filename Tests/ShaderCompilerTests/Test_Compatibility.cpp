#include "TestFramework.h"

#include <Fluxion/ShaderCompiler/ShaderCompiler.hpp>

#include <fstream>
#include <sstream>

using namespace Fluxion::ShaderCompiler;

namespace
{

std::string ReadFixture(const std::string& name)
{
    std::ifstream file(std::string(FLUXION_TEST_SHADERCOMPILER_FIXTURES_DIR) + "/" + name);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

IncludeResolver FixtureIncludeResolver()
{
    return [](const std::string& name, std::string& outContent) -> bool
    {
        std::ifstream file(std::string(FLUXION_TEST_SHADERCOMPILER_FIXTURES_DIR) + "/" + name);
        if (!file) return false;
        std::ostringstream contents;
        contents << file.rdbuf();
        outContent = contents.str();
        return true;
    };
}

void CompileFixture(TestContext& ctx, const char* fixtureName)
{
    std::string source = ReadFixture(fixtureName);
    TEST_CHECK(ctx, !source.empty());

    DiagnosticList diagnostics;
    CompileOptions options;
    options.stage = ShaderStage::Fragment;
    options.fileName = fixtureName;
    options.includeResolver = FixtureIncludeResolver();

    auto result = Compile(source, options, diagnostics);
    if (!result.IsOk())
    {
        for (const Diagnostic& d : diagnostics.entries)
            std::fprintf(stderr, "  %s:%u: %s\n", d.location.file.c_str(), d.location.line, d.message.c_str());
    }
    TEST_CHECK(ctx, result.IsOk());
}

} // namespace

void Test_Compatibility_Run(TestContext& ctx)
{
    // A larger, multi-file fixture corpus -- proves the frontend/
    // semantic analyzer/backends handle real, non-trivial source (a
    // shared #include, function overloading, texture sampling, uniforms,
    // swizzles, a real `return` routed to a [Target(N)]), not just the
    // small synthetic snippets the other unit tests use.
    CompileFixture(ctx, "common.jsl");
    CompileFixture(ctx, "lit.jsl");
}
