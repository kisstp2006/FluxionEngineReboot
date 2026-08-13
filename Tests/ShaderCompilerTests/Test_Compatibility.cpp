#include "TestFramework.h"

#include <Fluxion/Foundation/Hashing.h>

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

    {
        // What a compilation actually read has to come back with it. A
        // source that includes something else is only "the same source"
        // as long as that something else has not changed, and the source
        // text on its own cannot say whether it has -- so anything that
        // holds on to a compiler's answer needs this list, and needs it
        // to name what was read and to say what it contained.
        DiagnosticList diagnostics;
        CompileOptions options;
        options.stage = ShaderStage::Fragment;
        options.fileName = "lit.jsl";
        options.includeResolver = FixtureIncludeResolver();

        auto result = Compile(ReadFixture("lit.jsl"), options, diagnostics);
        TEST_CHECK(ctx, result.IsOk());
        if (result.IsOk())
        {
            const std::vector<ResolvedInclude>& includes = result.Value().includes;
            TEST_CHECK(ctx, includes.size() == 1);
            if (includes.size() == 1)
            {
                TEST_CHECK(ctx, includes[0].name == "common.jsl");

                // The hash is of what came back, so it has to match the
                // file itself -- not merely be non-zero, which an empty
                // or wrongly-read include could also manage.
                const std::string common = ReadFixture("common.jsl");
                TEST_CHECK(ctx, includes[0].contentHash == Fluxion_HashBytes64(common.data(), common.size()));
            }
        }

        // A source with nothing to read reports nothing read, rather than
        // whatever the previous call happened to leave behind.
        DiagnosticList plainDiagnostics;
        CompileOptions plainOptions;
        plainOptions.stage = ShaderStage::Fragment;
        plainOptions.fileName = "common.jsl";
        plainOptions.includeResolver = FixtureIncludeResolver();

        auto plain = Compile(ReadFixture("common.jsl"), plainOptions, plainDiagnostics);
        TEST_CHECK(ctx, plain.IsOk());
        if (plain.IsOk()) TEST_CHECK(ctx, plain.Value().includes.empty());
    }
}
