#include "TestFramework.h"

#include <Fluxion/Script/Script.hpp>

#include <cstdio>
#include <string>

using namespace Fluxion::Script;

namespace
{

void ReportDiagnostics(const DiagnosticList& diagnostics)
{
    for (const Diagnostic& entry : diagnostics.entries)
        std::fprintf(stderr, "  %s:%u:%u: %s\n", entry.location.file.c_str(), entry.location.line, entry.location.column, entry.message.c_str());
}

bool RunProgram(TestContext& ctx, const char* label, const std::string& source, const char* entry, ScriptValue& outValue)
{
    DiagnosticList diagnostics;
    CompileOptions options;
    options.fileName = label;

    auto compiled = Compile(source, options, diagnostics);
    if (!compiled.IsOk())
    {
        std::fprintf(stderr, "  FAIL: '%s' did not compile\n", label);
        ReportDiagnostics(diagnostics);
        ctx.failures++;
        return false;
    }

    Vm* vm = CreateVm(compiled.Value(), diagnostics);
    if (!vm)
    {
        std::fprintf(stderr, "  FAIL: '%s' did not load\n", label);
        ReportDiagnostics(diagnostics);
        ctx.failures++;
        return false;
    }

    auto result = Invoke(vm, entry);
    if (!result.IsOk())
    {
        std::fprintf(stderr, "  FAIL: '%s' faulted running %s: %s\n", label, entry,
            result.Status().message ? result.Status().message : "<no message>");
        ctx.failures++;
        DestroyVm(vm);
        return false;
    }

    outValue = result.Value();
    DestroyVm(vm);
    return true;
}

// A refusal has to carry a position, or the reader is left to find the
// problem by hand -- which is the whole of what a compiler is for.
void ExpectRejected(TestContext& ctx, const char* label, const std::string& source)
{
    DiagnosticList diagnostics;
    CompileOptions options;
    options.fileName = label;

    auto compiled = Compile(source, options, diagnostics);
    if (compiled.IsOk())
    {
        std::fprintf(stderr, "  FAIL: expected '%s' to be rejected\n", label);
        ctx.failures++;
        return;
    }

    const Diagnostic* first = nullptr;
    for (const Diagnostic& entry : diagnostics.entries)
    {
        if (entry.severity != DiagnosticSeverity::Error) continue;
        first = &entry;
        break;
    }

    if (!first)
    {
        std::fprintf(stderr, "  FAIL: '%s' was rejected without a diagnostic\n", label);
        ctx.failures++;
        return;
    }
    if (first->location.file != label)
    {
        std::fprintf(stderr, "  FAIL: '%s' was blamed on '%s': %s\n", label, first->location.file.c_str(), first->message.c_str());
        ctx.failures++;
        return;
    }
    if (first->location.line == 0 || first->location.column == 0)
    {
        std::fprintf(stderr, "  FAIL: '%s' reported at line %u column %u: %s\n",
            label, first->location.line, first->location.column, first->message.c_str());
        ctx.failures++;
    }
}

// Wraps a body in a program returning int, so the tests below stay about
// the conversion rather than about scaffolding.
std::string IntProgram(const char* body)
{
    return std::string("static class Program\n{\n    static int Main()\n    {\n") + body + "    }\n}\n";
}

std::string FloatProgram(const char* body)
{
    return std::string("static class Program\n{\n    static float Main()\n    {\n") + body + "    }\n}\n";
}

} // namespace

void Test_Casts_Run(TestContext& ctx)
{
    {
        // Truncation is toward zero on both signs. Rounding down instead
        // would agree with this on positives and disagree on negatives,
        // which is exactly the kind of difference that stays hidden until
        // something crosses zero.
        ScriptValue value;
        if (RunProgram(ctx, "truncates-toward-zero",
                IntProgram(
                    "        float up = 3.7f;\n"
                    "        float down = 0.0f - 3.7f;\n"
                    "        return (int)up * 100 + (int)down;\n"),
                "Program.Main", value))
        {
            // 3 * 100 + -3
            TEST_CHECK(ctx, value.type == ValueType::Int);
            TEST_CHECK(ctx, value.intValue == 297);
        }
    }
    {
        // Anything below the fractional part still has to be dropped, not
        // rounded: 2.999 is 2, and -0.5 is 0, not -1.
        ScriptValue value;
        if (RunProgram(ctx, "truncates-small-magnitudes",
                IntProgram(
                    "        float almost = 2.999f;\n"
                    "        float half = 0.0f - 0.5f;\n"
                    "        return (int)almost * 10 + (int)half;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 20);
        }
    }
    {
        // A float too large for an int has no honest answer, so the answer
        // is fixed rather than left to whatever the host happens to do --
        // narrowing an out-of-range float is undefined at the machine
        // level, and an interpreter must not hand that through.
        ScriptValue value;
        if (RunProgram(ctx, "saturates-out-of-range",
                IntProgram(
                    "        float zero = 0.0f;\n"
                    "        float one = 1.0f;\n"
                    "        float tooBig = one / zero;\n"
                    "        if ((int)tooBig != 2147483647) { return 1; }\n"
                    "        float tooSmall = (0.0f - one) / zero;\n"
                    "        if ((int)tooSmall != -2147483647 - 1) { return 2; }\n"
                    "        return 0;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }
    {
        // A value that is not a number narrows to zero. Same reasoning as
        // above: some answer has to be chosen, and it has to be the same
        // one everywhere.
        ScriptValue value;
        if (RunProgram(ctx, "not-a-number-narrows-to-zero",
                IntProgram(
                    "        float zero = 0.0f;\n"
                    "        float nothing = zero / zero;\n"
                    "        return (int)nothing;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }
    {
        // Widening is a real conversion to the narrower float format, not
        // a reinterpretation: 16777217 is the first whole number a float
        // cannot hold, and it has to come back as its neighbour.
        ScriptValue value;
        if (RunProgram(ctx, "widening-loses-precision-honestly",
                FloatProgram(
                    "        int big = 16777217;\n"
                    "        return (float)big;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Float);
            TEST_CHECK(ctx, value.floatValue == 16777216.0f);
        }
    }
    {
        // A value small enough to survive the trip has to survive it
        // exactly, in both directions.
        ScriptValue value;
        if (RunProgram(ctx, "round-trip-is-exact-when-it-fits",
                IntProgram(
                    "        int start = 1000000;\n"
                    "        return (int)(float)start - start;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }
    {
        // The conversion takes the operand next to it, not the arithmetic
        // around it. If it bound looser, this would read as (int)(2.9 * 2)
        // -- five instead of four -- and nothing would announce it.
        ScriptValue value;
        if (RunProgram(ctx, "binds-tighter-than-arithmetic",
                IntProgram(
                    "        float v = 2.9f;\n"
                    "        return (int)v * 2;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 4);
        }
    }
    {
        // A whole member and call chain belongs to the conversion, because
        // the chain is what produces the value being converted.
        ScriptValue value;
        if (RunProgram(ctx, "applies-to-a-member-and-call-chain",
                "class Holder\n"
                "{\n"
                "    int tag;\n"
                "    Holder(int t) { this.tag = t; }\n"
                "    int Doubled() { return this.tag * 2; }\n"
                "}\n"
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        Holder h = new Holder(9);\n"
                "        return (float)h.tag * 100000.0f + (float)h.Doubled();\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Float);
            TEST_CHECK(ctx, value.floatValue == 900018.0f);
        }
    }
    {
        // Converting to the type a value already has is allowed and
        // changes nothing -- worth stating, because generated code cannot
        // always tell in advance that it is a no-op.
        ScriptValue value;
        if (RunProgram(ctx, "same-type-conversion-is-a-no-op",
                IntProgram(
                    "        int i = 41;\n"
                    "        float f = 1.5f;\n"
                    "        if ((float)f != 1.5f) { return 1; }\n"
                    "        return (int)i + 1;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 42);
        }
    }
    {
        // Parentheses that are not a conversion must still group. This is
        // the parse the conversion syntax could plausibly have broken.
        ScriptValue value;
        if (RunProgram(ctx, "grouping-still-parses-as-grouping",
                IntProgram(
                    "        int a = 2;\n"
                    "        int b = 3;\n"
                    "        return (a + b) * 2 + (a);\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 12);
        }
    }
    {
        // A conversion may take a parenthesized expression, and may be
        // chained, without either reading as the other.
        ScriptValue value;
        if (RunProgram(ctx, "nested-and-chained-conversions",
                IntProgram(
                    "        int a = 3;\n"
                    "        int b = 4;\n"
                    "        float sum = (float)(a + b);\n"
                    "        return (int)(float)(int)(sum + 0.9f);\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 7);
        }
    }

    // What cannot be converted has to be refused, with a position. Silently
    // producing a number from a reference or a truth value would be the
    // worst possible outcome here.
    ExpectRejected(ctx, "reference-does-not-convert",
        "class Node { int v; Node() { this.v = 1; } }\n"
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        Node n = new Node();\n"
        "        return (int)n;\n"
        "    }\n"
        "}\n");

    ExpectRejected(ctx, "text-does-not-convert",
        IntProgram(
            "        string s = \"12\";\n"
            "        return (int)s;\n"));

    ExpectRejected(ctx, "truth-value-does-not-convert",
        IntProgram(
            "        bool b = true;\n"
            "        return (int)b;\n"));

    ExpectRejected(ctx, "named-constant-does-not-convert",
        "enum Facing { North, South }\n"
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        Facing f = Facing.South;\n"
        "        return (int)f;\n"
        "    }\n"
        "}\n");

    ExpectRejected(ctx, "value-type-does-not-convert",
        "struct P { float x; float y; }\n"
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        P p = new P();\n"
        "        return (int)p;\n"
        "    }\n"
        "}\n");
}
