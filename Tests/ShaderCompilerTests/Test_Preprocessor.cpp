#include "TestFramework.h"

#include <Fluxion/ShaderCompiler/Frontend/Preprocessor.hpp>

#include <string>

using namespace Fluxion::ShaderCompiler;

namespace
{

// The three bytes are written as escapes in their own string literal
// rather than pasted in as characters, for two reasons: a source file
// containing a real marker mid-line is the kind of thing an editor
// silently rewrites, and keeping them separate stops "\xBF" from
// swallowing the next character as another hex digit.
const char* const kBom = "\xEF\xBB\xBF";

IncludeResolver MakeResolver(const std::string& content)
{
    return [content](const std::string& name, std::string& outContent) -> bool
    {
        if (name != "shared.jsl") return false;
        outContent = content;
        return true;
    };
}

} // namespace

void Test_Preprocessor_Run(TestContext& ctx)
{
    {
        // The case this exists for. A marker in front of the '#' stops
        // the line being a directive at all, so it is copied out
        // verbatim: the include silently does not happen, and what
        // surfaces later is a missing definition somewhere else entirely.
        DiagnosticList diagnostics;
        const std::string source = std::string(kBom) + "#include \"shared.jsl\"\nfloat b = 2.0;\n";
        const std::string out = Preprocess(source, "<test>", MakeResolver("float a = 1.0;\n"), diagnostics);

        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, out.find("float a = 1.0;") != std::string::npos);
        TEST_CHECK(ctx, out.find("float b = 2.0;") != std::string::npos);

        // And the directive itself must not have survived into the
        // output -- that is exactly what the broken behaviour looked
        // like, and it would otherwise reach the lexer as garbage.
        TEST_CHECK(ctx, out.find("#include") == std::string::npos);
    }
    {
        // An included file can carry a marker of its own, and it is a
        // separate read: the top-level file being clean says nothing
        // about this one.
        DiagnosticList diagnostics;
        const std::string included = std::string(kBom) + "#define VALUE 3\n";
        const std::string source = "#include \"shared.jsl\"\n#ifdef VALUE\nfloat c = 3.0;\n#endif\n";
        const std::string out = Preprocess(source, "<test>", MakeResolver(included), diagnostics);

        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, out.find("float c = 3.0;") != std::string::npos);
    }
    {
        // Without a marker, nothing changes -- otherwise the checks
        // above could be passing because of something unrelated to the
        // marker at all.
        DiagnosticList diagnostics;
        const std::string out = Preprocess("#include \"shared.jsl\"\n", "<test>", MakeResolver("float a = 1.0;\n"), diagnostics);
        TEST_CHECK(ctx, !diagnostics.HasErrors());
        TEST_CHECK(ctx, out.find("float a = 1.0;") != std::string::npos);
    }
    {
        // A marker is not a licence to ignore a real failure: the
        // include still has to be reported as unresolvable.
        DiagnosticList diagnostics;
        const std::string source = std::string(kBom) + "#include \"missing.jsl\"\n";
        Preprocess(source, "<test>", MakeResolver("float a = 1.0;\n"), diagnostics);
        TEST_CHECK(ctx, diagnostics.HasErrors());
    }
}
