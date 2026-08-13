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

// Wraps a body in a static entry point, so each check below is only the
// arithmetic it is actually about.
std::string Program(const char* returnType, const std::string& body)
{
    return std::string("static class Program\n{\n    static ") + returnType + " Main()\n    {\n" + body + "    }\n}\n";
}

} // namespace

void Test_Mathf_Run(TestContext& ctx)
{
    {
        // Rounding is written as "add a half, take the floor". That is one
        // rule and it reads well, but it has a seam: for the largest float
        // below a half, the sum is not representable and lands exactly on
        // the next whole number, so the floor answers one instead of zero.
        // The value is below a half, so zero is the only right answer.
        ScriptValue value;
        if (RunProgram(ctx, "rounding-just-below-a-half",
                Program("float",
                    "        return Mathf.Round(0.49999997f);\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Float);
            TEST_CHECK(ctx, value.floatValue == 0.0f);
        }
    }
    {
        // Exactly halfway goes upwards, on both signs -- which means the
        // two are not mirror images: 1.5 goes to 2 but -1.5 goes to -1.
        // That is the stated rule; this holds it still so a later change
        // has to be a deliberate one.
        ScriptValue value;
        if (RunProgram(ctx, "rounding-at-a-half-on-both-signs",
                Program("int",
                    "        if (Mathf.Round(1.5f) != 2.0f) { return 1; }\n"
                    "        if (Mathf.Round(2.5f) != 3.0f) { return 2; }\n"
                    "        if (Mathf.Round(0.0f - 1.5f) != 0.0f - 1.0f) { return 3; }\n"
                    "        if (Mathf.Round(0.0f - 0.5f) != 0.0f) { return 4; }\n"
                    "        return 0;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }
    {
        // Below zero is where flooring and truncating part company, and
        // where a wrong one stays hidden in anything that never goes
        // negative.
        ScriptValue value;
        if (RunProgram(ctx, "floor-and-ceiling-below-zero",
                Program("int",
                    "        if (Mathf.Floor(0.0f - 2.5f) != 0.0f - 3.0f) { return 1; }\n"
                    "        if (Mathf.Ceil(0.0f - 2.5f) != 0.0f - 2.0f) { return 2; }\n"
                    "        if (Mathf.Floor(0.0f - 2.0f) != 0.0f - 2.0f) { return 3; }\n"
                    "        if (Mathf.Ceil(0.0f - 2.0f) != 0.0f - 2.0f) { return 4; }\n"
                    "        return 0;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }
    {
        // The stated rule for a low bound above the high one is that the
        // low one wins, and the two ends of an interpolation are held to
        // their stated behaviour as well.
        ScriptValue value;
        if (RunProgram(ctx, "bounds-that-disagree-and-interpolation-past-the-ends",
                Program("int",
                    "        if (Mathf.Clamp(5.0f, 10.0f, 0.0f) != 10.0f) { return 1; }\n"
                    "        if (Mathf.Clamp01(2.0f) != 1.0f) { return 2; }\n"
                    "        if (Mathf.Lerp(0.0f, 10.0f, 2.0f) != 10.0f) { return 3; }\n"
                    "        if (Mathf.LerpUnclamped(0.0f, 10.0f, 2.0f) != 20.0f) { return 4; }\n"
                    "        if (Mathf.InverseLerp(3.0f, 3.0f, 3.0f) != 0.0f) { return 5; }\n"
                    "        return 0;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }
    {
        // Zero cannot be stepped past by the generator's own arithmetic,
        // so a zero seed has to be moved aside -- otherwise every draw
        // from it is zero forever, which looks like a working generator
        // until someone reads the numbers.
        ScriptValue value;
        if (RunProgram(ctx, "a-zero-seed-still-produces-a-sequence",
                Program("int",
                    "        Random r = new Random(0);\n"
                    "        int a = r.NextInt();\n"
                    "        int b = r.NextInt();\n"
                    "        if (a == 0 || b == 0) { return 1; }\n"
                    "        if (a == b) { return 2; }\n"
                    "        Random negative = new Random(0 - 12345);\n"
                    "        int c = negative.NextInt();\n"
                    "        if (c <= 0) { return 3; }\n"
                    "        return 0;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }
    {
        // A bounded draw has to stay inside its bounds every single time,
        // not on average -- one escape in a thousand is a bug that only
        // shows up in someone else's run.
        ScriptValue value;
        if (RunProgram(ctx, "bounded-draws-stay-inside-their-bounds",
                Program("int",
                    "        Random r = new Random(7);\n"
                    "        for (int i = 0; i < 2000; i += 1)\n"
                    "        {\n"
                    "            int v = r.RangeInt(0 - 5, 5);\n"
                    "            if (v < 0 - 5) { return 1; }\n"
                    "            if (v > 4) { return 2; }\n"
                    "            float f = r.Range(0.0f - 1.0f, 1.0f);\n"
                    "            if (f < 0.0f - 1.0f) { return 3; }\n"
                    "            if (f > 1.0f) { return 4; }\n"
                    "        }\n"
                    "        if (r.RangeInt(5, 5) != 5) { return 5; }\n"
                    "        if (r.RangeInt(9, 2) != 9) { return 6; }\n"
                    "        return 0;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }
    {
        // Two generators given the same seed have to walk the same path,
        // and the same generator seeded again has to start over -- that is
        // the whole reason this one is written out by hand.
        ScriptValue value;
        if (RunProgram(ctx, "same-seed-same-path",
                Program("int",
                    "        Random a = new Random(2024);\n"
                    "        Random b = new Random(2024);\n"
                    "        for (int i = 0; i < 50; i += 1)\n"
                    "        {\n"
                    "            if (a.NextInt() != b.NextInt()) { return 1; }\n"
                    "        }\n"
                    "        a.SetSeed(2024);\n"
                    "        b.SetSeed(2024);\n"
                    "        if (a.NextInt() != b.NextInt()) { return 2; }\n"
                    "        Random other = new Random(2025);\n"
                    "        if (other.NextInt() == a.Seed()) { return 3; }\n"
                    "        return 0;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }

    // --- Mathf ----------------------------------------------------------
    //
    // Every answer below is a number that a float holds exactly, so each
    // check is an equality rather than a comparison against a tolerance:
    // what is being established is that the arithmetic is the arithmetic
    // that was written, not that it is close to it.
    {
        ScriptValue value;
        if (RunProgram(ctx, "the-plain-answers",
                Program("int",
                    "        if (Mathf.Abs(0.0f - 2.5f) != 2.5f) { return 0 - 1; }\n"
                    "        if (Mathf.Abs(2.5f) != 2.5f) { return 0 - 2; }\n"
                    "        if (Mathf.Min(2.0f, 5.0f) != 2.0f) { return 0 - 3; }\n"
                    "        if (Mathf.Max(2.0f, 5.0f) != 5.0f) { return 0 - 4; }\n"
                    "        if (Mathf.Min(5.0f, 5.0f) != 5.0f) { return 0 - 5; }\n"
                    "        return 0;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }
    {
        ScriptValue value;
        if (RunProgram(ctx, "holding-a-number-between-two-others",
                Program("int",
                    "        if (Mathf.Clamp(5.0f, 0.0f, 1.0f) != 1.0f) { return 0 - 1; }\n"
                    "        if (Mathf.Clamp(0.0f - 5.0f, 0.0f, 1.0f) != 0.0f) { return 0 - 2; }\n"
                    "        if (Mathf.Clamp(0.25f, 0.0f, 1.0f) != 0.25f) { return 0 - 3; }\n"
                    "        if (Mathf.Clamp01(2.0f) != 1.0f) { return 0 - 4; }\n"
                    "        if (Mathf.Clamp01(0.0f - 2.0f) != 0.0f) { return 0 - 5; }\n"
                    // A low bound above the high one: the low one is read
                    // first, so it is the one that wins.
                    "        if (Mathf.Clamp(0.5f, 1.0f, 0.0f) != 1.0f) { return 0 - 6; }\n"
                    "        return 0;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }
    {
        ScriptValue value;
        if (RunProgram(ctx, "moving-between-two-numbers",
                Program("int",
                    "        if (Mathf.Lerp(0.0f, 10.0f, 0.5f) != 5.0f) { return 0 - 1; }\n"
                    "        if (Mathf.Lerp(0.0f, 10.0f, 0.0f) != 0.0f) { return 0 - 2; }\n"
                    "        if (Mathf.Lerp(0.0f, 10.0f, 1.0f) != 10.0f) { return 0 - 3; }\n"
                    // Past either end, the usual form stops at the end.
                    "        if (Mathf.Lerp(0.0f, 10.0f, 2.0f) != 10.0f) { return 0 - 4; }\n"
                    "        if (Mathf.LerpUnclamped(0.0f, 10.0f, 2.0f) != 20.0f) { return 0 - 5; }\n"
                    "        if (Mathf.InverseLerp(0.0f, 10.0f, 2.5f) != 0.25f) { return 0 - 6; }\n"
                    "        if (Mathf.InverseLerp(4.0f, 4.0f, 9.0f) != 0.0f) { return 0 - 7; }\n"
                    "        return 0;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }
    {
        ScriptValue value;
        if (RunProgram(ctx, "rounding-in-each-direction",
                Program("int",
                    "        if (Mathf.Floor(2.75f) != 2.0f) { return 0 - 1; }\n"
                    "        if (Mathf.Ceil(2.25f) != 3.0f) { return 0 - 2; }\n"
                    "        if (Mathf.Floor(0.0f - 2.25f) != 0.0f - 3.0f) { return 0 - 3; }\n"
                    "        if (Mathf.Ceil(0.0f - 2.75f) != 0.0f - 2.0f) { return 0 - 4; }\n"
                    "        if (Mathf.Round(2.4f) != 2.0f) { return 0 - 5; }\n"
                    "        if (Mathf.Round(2.6f) != 3.0f) { return 0 - 6; }\n"
                    // Exactly halfway goes upwards, in both signs.
                    "        if (Mathf.Round(2.5f) != 3.0f) { return 0 - 7; }\n"
                    "        if (Mathf.Round(0.0f - 2.5f) != 0.0f - 2.0f) { return 0 - 8; }\n"
                    "        return 0;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }
    {
        // The three that cross over to the machine, checked on the
        // handful of inputs whose answers are exact.
        ScriptValue value;
        if (RunProgram(ctx, "the-answers-only-the-machine-has",
                Program("int",
                    "        if (Mathf.Sqrt(9.0f) != 3.0f) { return 0 - 1; }\n"
                    "        if (Mathf.Sqrt(0.0f) != 0.0f) { return 0 - 2; }\n"
                    // A square root of a negative number is not a number,
                    // so the nearest thing that still is one is given.
                    "        if (Mathf.Sqrt(0.0f - 9.0f) != 0.0f) { return 0 - 3; }\n"
                    "        if (Mathf.Sin(0.0f) != 0.0f) { return 0 - 4; }\n"
                    "        if (Mathf.Cos(0.0f) != 1.0f) { return 0 - 5; }\n"
                    "        if (Mathf.Abs(Mathf.Sin(Mathf.PI())) > 0.0001f) { return 0 - 6; }\n"
                    "        if (Mathf.Abs(Mathf.Cos(Mathf.PI()) + 1.0f) > 0.0001f) { return 0 - 7; }\n"
                    "        return 0;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }
    {
        // A whole number widens on its own, so the arithmetic reads the
        // same whichever a caller happens to have.
        ScriptValue value;
        if (RunProgram(ctx, "a-whole-number-is-accepted",
                Program("float", "        return Mathf.Max(3, 1.5f);\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Float);
            TEST_CHECK(ctx, value.floatValue == 3.0f);
        }
    }

    // --- Random -----------------------------------------------------------
    //
    // The whole worth of this is that it is the same every time, so what
    // is written down here is the sequence itself. A change to the step
    // that is not also a deliberate change to these numbers is a change
    // that has broken every recorded run there is.
    {
        ScriptValue value;
        if (RunProgram(ctx, "one-seed-gives-one-sequence",
                Program("int",
                    "        Random source = new Random(1);\n"
                    "        if (source.NextInt() != 16807) { return 0 - 1; }\n"
                    "        if (source.NextInt() != 282475249) { return 0 - 2; }\n"
                    "        if (source.NextInt() != 1622650073) { return 0 - 3; }\n"
                    "        if (source.NextInt() != 984943658) { return 0 - 4; }\n"
                    "        return 0;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }
    {
        // Two of them given the same seed walk the same path, and one
        // told the seed again starts the path over.
        ScriptValue value;
        if (RunProgram(ctx, "the-same-seed-is-the-same-sequence",
                Program("int",
                    "        Random first = new Random(12345);\n"
                    "        Random second = new Random(12345);\n"
                    "        for (int i = 0; i < 32; i += 1)\n"
                    "        {\n"
                    "            if (first.NextInt() != second.NextInt()) { return 0 - 1; }\n"
                    "        }\n"
                    "\n"
                    "        int afterReset = 0;\n"
                    "        first.SetSeed(12345);\n"
                    "        second.SetSeed(12345);\n"
                    "        if (first.NextInt() != second.NextInt()) { return 0 - 2; }\n"
                    "        return afterReset;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }
    {
        // Different seeds do not walk the same path.
        ScriptValue value;
        if (RunProgram(ctx, "different-seeds-differ",
                Program("bool",
                    "        Random first = new Random(1);\n"
                    "        Random second = new Random(2);\n"
                    "        return first.NextInt() != second.NextInt();\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.boolValue);
        }
    }
    {
        // Every seed a caller could write is welcomed, including the two
        // the step itself could not use.
        ScriptValue value;
        if (RunProgram(ctx, "every-seed-is-usable",
                Program("int",
                    "        Random zero = new Random(0);\n"
                    "        if (zero.Seed() != 1) { return 0 - 1; }\n"
                    "        Random negative = new Random(0 - 5);\n"
                    "        if (negative.Seed() != 2147483642) { return 0 - 2; }\n"
                    "        if (negative.NextInt() <= 0) { return 0 - 3; }\n"
                    "        return 0;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }
    {
        // Whatever the sequence is, it stays inside the bounds each shape
        // of the question promises.
        ScriptValue value;
        if (RunProgram(ctx, "the-answers-stay-in-range",
                Program("int",
                    "        Random source = new Random(7);\n"
                    "        for (int i = 0; i < 512; i += 1)\n"
                    "        {\n"
                    "            float unit = source.NextFloat();\n"
                    "            if (unit < 0.0f || unit >= 1.0f) { return 0 - 1; }\n"
                    "\n"
                    "            float spread = source.Range(0.0f - 2.0f, 3.0f);\n"
                    "            if (spread < 0.0f - 2.0f || spread >= 3.0f) { return 0 - 2; }\n"
                    "\n"
                    "            int rolled = source.RangeInt(1, 7);\n"
                    "            if (rolled < 1 || rolled > 6) { return 0 - 3; }\n"
                    "        }\n"
                    // A span with nothing in it has only one answer it
                    // can give.
                    "        if (source.RangeInt(4, 4) != 4) { return 0 - 4; }\n"
                    "        if (source.RangeInt(9, 2) != 9) { return 0 - 5; }\n"
                    "        return 0;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }
    {
        // Both faces of a coin turn up over enough throws, which no
        // sequence stuck on one number could manage.
        ScriptValue value;
        if (RunProgram(ctx, "a-coin-lands-both-ways",
                Program("bool",
                    "        Random source = new Random(99);\n"
                    "        int heads = 0;\n"
                    "        for (int i = 0; i < 64; i += 1)\n"
                    "        {\n"
                    "            if (source.NextBool()) { heads += 1; }\n"
                    "        }\n"
                    "        return heads > 0 && heads < 64;\n"),
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.boolValue);
        }
    }
}
