// The contents of this file are subject to the Common Public Attribution
// License Version 1.0 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// https://opensource.org/license/cpal-1-0. The License is based on the
// Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
// to cover use of software over a computer network and provide for limited
// attribution for the Original Developer. In addition, Exhibit A has been
// modified to be consistent with Exhibit B.
//
// Software distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
// for the specific language governing rights and limitations under the
// License.
//
// The Original Code is Fluxion Engine.
//
// The Original Developer is not the Initial Developer and is __________. If
// left blank, the Original Developer is the Initial Developer.
//
// The Initial Developer of the Original Code is Kiss Tibor Péter. All
// portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
// All Rights Reserved.
//
// Contributor ______________________.
//
// Alternatively, the contents of this file may be used under the terms of
// the Fluxion Engine Commercial License Agreement Version 1.0, separately
// obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
// License"), in which case the provisions of the Commercial License are
// applicable instead of those above.
//
// If you wish to allow use of your version of this file only under the terms
// of the Commercial License and not to allow others to use your version of
// this file under the CPAL, indicate your decision by deleting the
// provisions above and replace them with the notice and other provisions
// required by the Commercial License. If you do not delete the provisions
// above, a recipient may use your version of this file under either the CPAL
// or the Commercial License.
//
// SPDX-License-Identifier: CPAL-1.0

#include "TestFramework.h"

#include <Fluxion/Script/Script.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace Fluxion::Script;

namespace
{

struct Capture
{
    std::string all;
    std::string console;
    std::string logInfo;
    std::string logWarning;
    std::string logError;
};

void CaptureOutput(void* user, OutputChannel channel, const char* text)
{
    auto* capture = static_cast<Capture*>(user);
    capture->all += text;
    switch (channel)
    {
        case OutputChannel::Console: capture->console += text; break;
        case OutputChannel::LogInfo: capture->logInfo += text; break;
        case OutputChannel::LogWarning: capture->logWarning += text; break;
        case OutputChannel::LogError: capture->logError += text; break;
        default: break;
    }
}

void ReportDiagnostics(const DiagnosticList& diagnostics)
{
    for (const Diagnostic& entry : diagnostics.entries)
        std::fprintf(stderr, "  %s:%u:%u: %s\n", entry.location.file.c_str(), entry.location.line, entry.location.column, entry.message.c_str());
}

// Compiles the source, runs one named method with output redirected into
// `capture`, and hands back what the method returned.
bool RunProgram(TestContext& ctx, const char* label, const std::string& source, const char* entry,
                Capture& capture, ScriptValue& outValue)
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

    SetOutputHandler(vm, &CaptureOutput, &capture);

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

} // namespace

void Test_VmExecution_Run(TestContext& ctx)
{
    {
        // A method may call one declared later in the file, and one that
        // lives in another class.
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "forward-and-cross-class-calls",
                "static class Program { static int Main() { return Later(3) + Helpers.Triple(2); } static int Later(int n) { return n * n; } }\n"
                "static class Helpers { static int Triple(int n) { return n * 3; } }\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.intValue == 15); // 9 + 6
        }
    }
    {
        // A declaration inside a block shadows the outer one for that block
        // only; the outer variable keeps its own storage and value.
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "block-shadowing",
                "static class Program { static int Main() { int x = 1; { int x = 20; x += 5; } return x; } }\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.intValue == 1);
        }
    }
    {
        // Sibling blocks each get their own locals -- reusing a slot must not
        // leak the earlier block's value into the later one.
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "sibling-block-locals",
                "static class Program { static int Main() { int total = 0; { int a = 7; total += a; } { int b = 2; total += b; } return total; } }\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.intValue == 9);
        }
    }
    {
        // Runaway recursion has to come back as a reported fault, not a
        // crash or a silently wrong answer.
        DiagnosticList diagnostics;
        CompileOptions options;
        options.fileName = "runaway-recursion";
        auto compiled = Compile(
            "static class Program { static int Main() { return Deep(100000); } static int Deep(int n) { if (n == 0) { return 0; } return 1 + Deep(n - 1); } }\n",
            options, diagnostics);
        TEST_CHECK(ctx, compiled.IsOk());
        if (compiled.IsOk())
        {
            Vm* vm = CreateVm(compiled.Value(), diagnostics);
            TEST_CHECK(ctx, vm != nullptr);
            if (vm)
            {
                auto result = Invoke(vm, "Program.Main");
                TEST_CHECK(ctx, !result.IsOk());
                TEST_CHECK(ctx, result.Status().message != nullptr);
                DestroyVm(vm);
            }
        }
    }
    {
        // An assignment is an expression: it stores, and also yields the
        // value it stored, so it can feed a surrounding expression.
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "assignment-yields-value",
                "static class Program { static int Main() { int a = 0; int b = (a = 5) + 1; return a * 100 + b; } }\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Int);
            TEST_CHECK(ctx, value.intValue == 506);
        }
    }
    {
        // `continue` skips the rest of the body but must NOT skip the step,
        // or the loop never terminates.
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "for-continue-runs-step",
                "static class Program { static int Main() { int total = 0; for (int i = 0; i < 6; i += 1) { if (i % 2 == 0) { continue; } total += i; } return total; } }\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.intValue == 9); // 1 + 3 + 5
        }
    }
    {
        // `break` leaves only the innermost loop.
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "nested-break",
                "static class Program { static int Main() { int hits = 0; for (int i = 0; i < 3; i += 1) { for (int j = 0; j < 5; j += 1) { if (j == 2) { break; } hits += 1; } } return hits; } }\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.intValue == 6); // 2 inner iterations x 3 outer
        }
    }
    {
        // A `for` with no condition relies on `break` to end.
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "for-without-condition",
                "static class Program { static int Main() { int n = 0; for (;;) { n += 1; if (n == 4) { break; } } return n; } }\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.intValue == 4);
        }
    }
    {
        // Unary minus binds tighter than the binary operators around it.
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "unary-precedence",
                "static class Program { static int Main() { return -2 * 3 + 10 - -3; } }\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.intValue == 7); // (-2*3) + 10 + 3
        }
    }
    {
        // A compound assignment on a float target promotes an int operand.
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "compound-assign-promotes",
                "static class Program { static float Main() { float f = 1.5f; f += 2; f *= 2; return f; } }\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Float);
            TEST_CHECK(ctx, value.floatValue == 7.0f);
        }
    }
    {
        // Multiplication and division are applied before addition and
        // subtraction, and the whole thing runs on int opcodes.
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "int-arithmetic",
                "static class Program { static int Main() { return 2 + 3 * 4 - 6 / 3; } }\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Int);
            TEST_CHECK(ctx, value.intValue == 12);
        }
    }
    {
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "int-division",
                "static class Program { static int Main() { return 17 / 5 + 17 % 5; } }\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.intValue == 5);
        }
    }
    {
        // An int reaching a float context is widened, both in the
        // initializer and inside the arithmetic.
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "float-promotion",
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        float f = 3;\n"
                "        f = f * 2 + 0.5f;\n"
                "        return f;\n"
                "    }\n"
                "}\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Float);
            TEST_CHECK(ctx, value.floatValue == 6.5f);
        }
    }
    {
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "string-concatenation",
                "static class Program\n"
                "{\n"
                "    static string Main()\n"
                "    {\n"
                "        int count = 3;\n"
                "        return \"count=\" + count + \" ratio=\" + 0.5f + \" ok=\" + true;\n"
                "    }\n"
                "}\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.type == ValueType::String);
            TEST_CHECK(ctx, value.stringValue == "count=3 ratio=0.5 ok=true");
        }
    }
    {
        // Both arms of a branch are reachable and produce their own
        // result.
        const std::string source =
            "static class Program\n"
            "{\n"
            "    static string Classify(int v)\n"
            "    {\n"
            "        if (v > 5) { return \"big\"; }\n"
            "        else { return \"small\"; }\n"
            "    }\n"
            "    static string Big() { return Classify(10); }\n"
            "    static string Small() { return Classify(1); }\n"
            "}\n";

        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "branch-then", source, "Program.Big", capture, value))
            TEST_CHECK(ctx, value.stringValue == "big");

        Capture otherCapture;
        ScriptValue otherValue;
        if (RunProgram(ctx, "branch-else", source, "Program.Small", otherCapture, otherValue))
            TEST_CHECK(ctx, otherValue.stringValue == "small");
    }
    {
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "while-total",
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        int total = 0;\n"
                "        int i = 1;\n"
                "        while (i <= 5)\n"
                "        {\n"
                "            total += i;\n"
                "            i += 1;\n"
                "        }\n"
                "        return total;\n"
                "    }\n"
                "}\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.intValue == 15);
        }
    }
    {
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "for-total",
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        int total = 0;\n"
                "        for (int i = 0; i < 5; i += 1) { total += i; }\n"
                "        return total;\n"
                "    }\n"
                "}\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.intValue == 10);
        }
    }
    {
        // `continue` skips the rest of the body but still runs the step,
        // and `break` leaves the loop entirely.
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "break-and-continue",
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        int total = 0;\n"
                "        for (int i = 0; i < 10; i += 1)\n"
                "        {\n"
                "            if (i == 5) { break; }\n"
                "            if (i % 2 == 0) { continue; }\n"
                "            total += i;\n"
                "        }\n"
                "        return total;\n"
                "    }\n"
                "}\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.intValue == 4);
        }
    }
    {
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "while-break-continue",
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        int total = 0;\n"
                "        int i = 0;\n"
                "        while (true)\n"
                "        {\n"
                "            i += 1;\n"
                "            if (i > 6) { break; }\n"
                "            if (i == 3) { continue; }\n"
                "            total += i;\n"
                "        }\n"
                "        return total;\n"
                "    }\n"
                "}\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.intValue == 18);
        }
    }
    {
        // Recursion proves each call gets its own frame: the parameters
        // of an outer call survive the inner ones.
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "recursion",
                "static class Program\n"
                "{\n"
                "    static int Factorial(int n)\n"
                "    {\n"
                "        if (n <= 1) { return 1; }\n"
                "        return n * Factorial(n - 1);\n"
                "    }\n"
                "    static int Fib(int n)\n"
                "    {\n"
                "        if (n < 2) { return n; }\n"
                "        return Fib(n - 1) + Fib(n - 2);\n"
                "    }\n"
                "    static int Main() { return Factorial(6) + Fib(10); }\n"
                "}\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.intValue == 775);
        }
    }
    {
        // Each built-in reaches the channel it belongs to, and the
        // line-oriented forms bring their own newline.
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "output-channels",
                "static class Program\n"
                "{\n"
                "    static void Main()\n"
                "    {\n"
                "        Console.Write(\"a\");\n"
                "        Console.WriteLine(\"b\");\n"
                "        Console.WriteLine(7);\n"
                "        Console.WriteLine(1.5f);\n"
                "        Console.WriteLine(true);\n"
                "        Debug.Log(\"info\");\n"
                "        Debug.LogWarning(\"warn\");\n"
                "        Debug.LogError(\"err\");\n"
                "    }\n"
                "}\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Void);
            TEST_CHECK(ctx, capture.console == "ab\n7\n1.5\ntrue\n");
            TEST_CHECK(ctx, capture.logInfo == "info\n");
            TEST_CHECK(ctx, capture.logWarning == "warn\n");
            TEST_CHECK(ctx, capture.logError == "err\n");
        }
    }
    {
        // The right-hand side of a logical operator is only reached when
        // the left one leaves the answer open.
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "short-circuit",
                "static class Program\n"
                "{\n"
                "    static bool Loud() { Debug.Log(\"reached\"); return true; }\n"
                "    static bool Main() { return (true || Loud()) && !(false && Loud()); }\n"
                "}\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Bool);
            TEST_CHECK(ctx, value.boolValue);
            TEST_CHECK(ctx, capture.logInfo.empty());
        }
    }
    {
        // ... and it is reached when the left one does not settle it.
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "short-circuit-taken",
                "static class Program\n"
                "{\n"
                "    static bool Loud() { Debug.Log(\"reached\"); return false; }\n"
                "    static bool Main() { return false || Loud(); }\n"
                "}\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, !value.boolValue);
            TEST_CHECK(ctx, capture.logInfo == "reached\n");
        }
    }
    {
        // Strings compare by content, not by which slot they came from.
        Capture capture;
        ScriptValue value;
        if (RunProgram(ctx, "string-equality",
                "static class Program\n"
                "{\n"
                "    static bool Main()\n"
                "    {\n"
                "        string a = \"ab\";\n"
                "        string b = \"a\" + \"b\";\n"
                "        return a == b && a != \"other\";\n"
                "    }\n"
                "}\n",
                "Program.Main", capture, value))
        {
            TEST_CHECK(ctx, value.boolValue);
        }
    }
    {
        // Names that do not exist, and methods that need arguments, are
        // refused rather than run.
        DiagnosticList diagnostics;
        CompileOptions options;
        options.fileName = "<test>";
        auto compiled = Compile(
            "static class Program { static int Twice(int v) { return v * 2; } static void Main() { } }\n",
            options, diagnostics);
        TEST_CHECK(ctx, compiled.IsOk());
        if (compiled.IsOk())
        {
            Vm* vm = CreateVm(compiled.Value(), diagnostics);
            TEST_CHECK(ctx, vm != nullptr);
            if (vm)
            {
                auto missing = Invoke(vm, "Program.Nowhere");
                TEST_CHECK(ctx, !missing.IsOk());
                TEST_CHECK(ctx, missing.Status().code == 2);

                auto needsArguments = Invoke(vm, "Program.Twice");
                TEST_CHECK(ctx, !needsArguments.IsOk());
                TEST_CHECK(ctx, needsArguments.Status().code == 3);

                auto fine = Invoke(vm, "Program.Main");
                TEST_CHECK(ctx, fine.IsOk());
                DestroyVm(vm);
            }
        }
    }
    {
        // A division by zero is reported as a fault instead of taking the
        // host down with it.
        DiagnosticList diagnostics;
        CompileOptions options;
        options.fileName = "<test>";
        auto compiled = Compile(
            "static class Program { static int Main() { int zero = 0; return 1 / zero; } }\n",
            options, diagnostics);
        TEST_CHECK(ctx, compiled.IsOk());
        if (compiled.IsOk())
        {
            Vm* vm = CreateVm(compiled.Value(), diagnostics);
            TEST_CHECK(ctx, vm != nullptr);
            if (vm)
            {
                auto result = Invoke(vm, "Program.Main");
                TEST_CHECK(ctx, !result.IsOk());
                TEST_CHECK(ctx, result.Status().message != nullptr);
                DestroyVm(vm);
            }
        }
    }
    {
        // Unbounded recursion stops at the depth limit rather than
        // exhausting the host's stack.
        DiagnosticList diagnostics;
        CompileOptions options;
        options.fileName = "<test>";
        auto compiled = Compile(
            "static class Program { static int Down(int n) { return Down(n + 1); } static int Main() { return Down(0); } }\n",
            options, diagnostics);
        TEST_CHECK(ctx, compiled.IsOk());
        if (compiled.IsOk())
        {
            Vm* vm = CreateVm(compiled.Value(), diagnostics);
            TEST_CHECK(ctx, vm != nullptr);
            if (vm)
            {
                auto result = Invoke(vm, "Program.Main");
                TEST_CHECK(ctx, !result.IsOk());
                DestroyVm(vm);
            }
        }
    }
}

void Test_VmExecution_Fixture_Run(TestContext& ctx)
{
    const std::string path = std::string(FLUXION_TEST_SCRIPT_FIXTURES_DIR) + "/sample.fls";

    std::ifstream file(path);
    std::ostringstream contents;
    contents << file.rdbuf();
    const std::string source = contents.str();

    TEST_CHECK(ctx, !source.empty());
    if (source.empty()) return;

    Capture capture;
    ScriptValue value;
    if (!RunProgram(ctx, "sample.fls", source, "Program.Main", capture, value)) return;

    TEST_CHECK(ctx, value.type == ValueType::Int);
    TEST_CHECK(ctx, value.intValue == 39);
    TEST_CHECK(ctx, capture.console == "squares=30\n1.75\n");
    TEST_CHECK(ctx, capture.logInfo == "done\n");
    TEST_CHECK(ctx, capture.logWarning.empty());
}
