#include "TestFramework.h"

#include <Fluxion/Script/Compiler/Lexer.hpp>
#include <Fluxion/Script/Compiler/Parser.hpp>
#include <Fluxion/Script/Compiler/Semantic.hpp>

#include <cstdio>
#include <string>

using namespace Fluxion::Script;

namespace
{

bool AnalyzeSource(const char* source, DiagnosticList& diagnostics)
{
    std::vector<Token> tokens = Lex(source, "<test>", diagnostics);
    Program program = Parse(tokens, diagnostics);
    if (diagnostics.HasErrors()) return false;
    return Analyze(program, diagnostics);
}

const Diagnostic* FirstError(const DiagnosticList& diagnostics)
{
    for (const Diagnostic& entry : diagnostics.entries)
    {
        if (entry.severity == DiagnosticSeverity::Error) return &entry;
    }
    return nullptr;
}

// Every rejection must arrive with a usable position: a message that
// cannot say where the problem is costs more than it gives.
void ExpectRejected(TestContext& ctx, const char* label, const char* source)
{
    DiagnosticList diagnostics;
    const bool accepted = AnalyzeSource(source, diagnostics);

    if (accepted)
    {
        std::fprintf(stderr, "  FAIL: expected '%s' to be rejected\n", label);
        ctx.failures++;
        return;
    }

    const Diagnostic* error = FirstError(diagnostics);
    if (!error)
    {
        std::fprintf(stderr, "  FAIL: '%s' was rejected without a diagnostic\n", label);
        ctx.failures++;
        return;
    }

    if (error->location.line == 0 || error->location.column == 0)
    {
        std::fprintf(stderr, "  FAIL: '%s' reported at line %u column %u: %s\n",
            label, error->location.line, error->location.column, error->message.c_str());
        ctx.failures++;
        return;
    }

    if (error->location.file != "<test>")
    {
        std::fprintf(stderr, "  FAIL: '%s' reported against file '%s'\n", label, error->location.file.c_str());
        ctx.failures++;
    }
}

} // namespace

void Test_Semantic_Run(TestContext& ctx)
{
    {
        DiagnosticList diagnostics;
        const bool accepted = AnalyzeSource(
            "static class Program\n"
            "{\n"
            "    static int Add(int a, int b)\n"
            "    {\n"
            "        return a + b;\n"
            "    }\n"
            "\n"
            "    static void Main()\n"
            "    {\n"
            "        int x = 3;\n"
            "        float f = 2.5f;\n"
            "        bool flag = true;\n"
            "        string name = \"world\";\n"
            "        var sum = Add(x, 4);\n"
            "\n"
            "        if (sum > 5) { Console.WriteLine(\"big\"); }\n"
            "        else { Debug.Log(\"small\"); }\n"
            "\n"
            "        while (sum > 0) { sum -= 1; }\n"
            "\n"
            "        for (int i = 0; i < 3; i += 1)\n"
            "        {\n"
            "            Console.WriteLine(\"hello \" + name);\n"
            "        }\n"
            "\n"
            "        f = x;\n"
            "        flag = !flag && sum == 0;\n"
            "    }\n"
            "}\n",
            diagnostics);
        if (!accepted)
        {
            for (const Diagnostic& entry : diagnostics.entries)
                std::fprintf(stderr, "  %s:%u:%u: %s\n", entry.location.file.c_str(), entry.location.line, entry.location.column, entry.message.c_str());
        }
        TEST_CHECK(ctx, accepted);
    }

    ExpectRejected(ctx, "undeclared identifier",
        "static class P\n"
        "{\n"
        "    static void M() { int y = missing; }\n"
        "}\n");

    ExpectRejected(ctx, "string assigned to an int",
        "static class P\n"
        "{\n"
        "    static void M() { int y = \"text\"; }\n"
        "}\n");

    ExpectRejected(ctx, "call to an unknown method",
        "static class P\n"
        "{\n"
        "    static void M() { Nowhere(1); }\n"
        "}\n");

    ExpectRejected(ctx, "call to an unknown qualified method",
        "static class P\n"
        "{\n"
        "    static void M() { Other.Thing(1); }\n"
        "}\n");

    ExpectRejected(ctx, "wrong argument count",
        "static class P\n"
        "{\n"
        "    static int Add(int a, int b) { return a + b; }\n"
        "    static void M() { Add(1); }\n"
        "}\n");

    ExpectRejected(ctx, "wrong argument type",
        "static class P\n"
        "{\n"
        "    static int Add(int a, int b) { return a + b; }\n"
        "    static void M() { Add(1, \"two\"); }\n"
        "}\n");

    ExpectRejected(ctx, "if on a non-bool condition",
        "static class P\n"
        "{\n"
        "    static void M() { int x = 1; if (x) { } }\n"
        "}\n");

    ExpectRejected(ctx, "value-less return from a method that returns a value",
        "static class P\n"
        "{\n"
        "    static int M() { return; }\n"
        "}\n");

    ExpectRejected(ctx, "break outside a loop",
        "static class P\n"
        "{\n"
        "    static void M() { break; }\n"
        "}\n");

    ExpectRejected(ctx, "continue outside a loop",
        "static class P\n"
        "{\n"
        "    static void M() { continue; }\n"
        "}\n");

    ExpectRejected(ctx, "float narrowed to an int",
        "static class P\n"
        "{\n"
        "    static void M() { int x = 1.5f; }\n"
        "}\n");

    ExpectRejected(ctx, "inferred declaration without an initializer",
        "static class P\n"
        "{\n"
        "    static void M() { var x; }\n"
        "}\n");

    ExpectRejected(ctx, "inferred declaration from a method that returns nothing",
        "static class P\n"
        "{\n"
        "    static void Nothing() { }\n"
        "    static void M() { var x = Nothing(); }\n"
        "}\n");

    ExpectRejected(ctx, "value returned from a method that returns nothing",
        "static class P\n"
        "{\n"
        "    static void M() { return 1; }\n"
        "}\n");

    ExpectRejected(ctx, "arithmetic on a bool",
        "static class P\n"
        "{\n"
        "    static void M() { bool b = true; int x = b + 1; }\n"
        "}\n");

    ExpectRejected(ctx, "number joined to a string on the wrong side",
        "static class P\n"
        "{\n"
        "    static void M() { string s = 1 + \"x\"; }\n"
        "}\n");

    ExpectRejected(ctx, "built-in given an argument type it does not accept",
        "static class P\n"
        "{\n"
        "    static void M() { bool b = true; Debug.Log(b); }\n"
        "}\n");

    ExpectRejected(ctx, "duplicate local name",
        "static class P\n"
        "{\n"
        "    static void M() { int x = 1; int x = 2; }\n"
        "}\n");

    {
        // Widening flows in one direction only: an int reaches a float
        // parameter, a float never reaches an int one.
        DiagnosticList diagnostics;
        const bool accepted = AnalyzeSource(
            "static class P\n"
            "{\n"
            "    static float Half(float v) { return v / 2; }\n"
            "    static void M() { float r = Half(7); Console.WriteLine(r); }\n"
            "}\n",
            diagnostics);
        TEST_CHECK(ctx, accepted);
    }
    {
        // A string absorbs each of the other types on its right.
        DiagnosticList diagnostics;
        const bool accepted = AnalyzeSource(
            "static class P\n"
            "{\n"
            "    static void M()\n"
            "    {\n"
            "        string s = \"n=\" + 1;\n"
            "        s = s + 2.5f;\n"
            "        s = s + true;\n"
            "        s += \" done\";\n"
            "        Console.WriteLine(s);\n"
            "    }\n"
            "}\n",
            diagnostics);
        TEST_CHECK(ctx, accepted);
    }
    {
        // Methods of another class are reachable by their qualified name.
        DiagnosticList diagnostics;
        const bool accepted = AnalyzeSource(
            "static class Helper { static int Twice(int v) { return v * 2; } }\n"
            "static class P { static int M() { return Helper.Twice(21); } }\n",
            diagnostics);
        TEST_CHECK(ctx, accepted);
    }
}
