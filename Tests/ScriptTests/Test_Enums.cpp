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

// A set that starts at zero, jumps where it is told to, and picks up
// counting from wherever it was left.
const char* const kKeyCode =
    "enum KeyCode\n"
    "{\n"
    "    Unknown,\n"
    "    A,\n"
    "    B = 10,\n"
    "    C,\n"
    "    D = -1,\n"
    "}\n";

} // namespace

void Test_Enums_Run(TestContext& ctx)
{
    {
        // What each constant is worth: counting from zero, restarting
        // wherever a number was written, and carrying on from there.
        ScriptValue value;
        if (RunProgram(ctx, "constants-have-their-declared-values",
                std::string(kKeyCode) +
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        int unknown = 0;\n"
                "        if (KeyCode.Unknown != KeyCode.Unknown) { return 0 - 1; }\n"
                "\n"
                "        KeyCode a = KeyCode.A;\n"
                "        KeyCode b = KeyCode.B;\n"
                "        KeyCode c = KeyCode.C;\n"
                "\n"
                "        if (a == b) { return 0 - 2; }\n"
                "        if (b == c) { return 0 - 3; }\n"
                "        if (!(a == KeyCode.A)) { return 0 - 4; }\n"
                "        if (a != KeyCode.A) { return 0 - 5; }\n"
                "        return unknown;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
    }
    {
        // The numbers themselves, read back one at a time. A constant is
        // settled while the source is read, so what a method answers with
        // is the number it was declared with.
        ScriptValue value;
        if (RunProgram(ctx, "the-numbers-behind-the-names",
                std::string(kKeyCode) +
                "static class Program\n"
                "{\n"
                "    static KeyCode Pick(int which)\n"
                "    {\n"
                "        if (which == 0) { return KeyCode.Unknown; }\n"
                "        if (which == 1) { return KeyCode.A; }\n"
                "        if (which == 2) { return KeyCode.B; }\n"
                "        if (which == 3) { return KeyCode.C; }\n"
                "        return KeyCode.D;\n"
                "    }\n"
                "    static KeyCode Main() { return Pick(3); }\n"
                "}\n",
                "Program.Main", value))
        {
            // C follows an explicit 10, so it is 11.
            TEST_CHECK(ctx, value.type == ValueType::Enum);
            TEST_CHECK(ctx, value.intValue == 11);
        }
    }
    {
        ScriptValue value;
        if (RunProgram(ctx, "an-explicit-value-is-taken-as-written",
                std::string(kKeyCode) +
                "static class Program\n"
                "{\n"
                "    static KeyCode Main() { return KeyCode.B; }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 10);
        }
    }
    {
        ScriptValue value;
        if (RunProgram(ctx, "counting-starts-at-zero",
                std::string(kKeyCode) +
                "static class Program\n"
                "{\n"
                "    static KeyCode Main() { return KeyCode.A; }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 1);
        }
    }
    {
        ScriptValue value;
        if (RunProgram(ctx, "a-negative-value-is-allowed",
                std::string(kKeyCode) +
                "static class Program\n"
                "{\n"
                "    static KeyCode Main() { return KeyCode.D; }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == -1);
        }
    }
    {
        // A field, a parameter and an answer all carry one unchanged,
        // through an object and back out again.
        ScriptValue value;
        if (RunProgram(ctx, "a-constant-travels-through-a-class",
                std::string(kKeyCode) +
                "class Binding\n"
                "{\n"
                "    KeyCode key;\n"
                "    int uses;\n"
                "\n"
                "    Binding(KeyCode k) { this.key = k; }\n"
                "\n"
                "    KeyCode Key() { return this.key; }\n"
                "    void Rebind(KeyCode k) { this.key = k; this.uses = this.uses + 1; }\n"
                "    bool Matches(KeyCode k) { return this.key == k; }\n"
                "}\n"
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        Binding binding = new Binding(KeyCode.B);\n"
                "        if (!binding.Matches(KeyCode.B)) { return 0 - 1; }\n"
                "        if (binding.Matches(KeyCode.C)) { return 0 - 2; }\n"
                "\n"
                "        binding.Rebind(KeyCode.C);\n"
                "        if (!binding.Matches(KeyCode.C)) { return 0 - 3; }\n"
                "        if (binding.Key() == KeyCode.B) { return 0 - 4; }\n"
                "\n"
                "        return binding.uses;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 1);
        }
    }
    {
        // A field that was never written holds the first thing a zero
        // stands for, exactly as a number field holds zero.
        ScriptValue value;
        if (RunProgram(ctx, "an-unwritten-field-is-zero",
                std::string(kKeyCode) +
                "class Binding { KeyCode key; Binding() { } }\n"
                "static class Program\n"
                "{\n"
                "    static bool Main() { return new Binding().key == KeyCode.Unknown; }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.boolValue);
        }
    }
    {
        // A named constant is one slot and a value, so it sits inside a
        // value type as happily as a number does.
        ScriptValue value;
        if (RunProgram(ctx, "a-constant-inside-a-value-type",
                std::string(kKeyCode) +
                "struct Stroke\n"
                "{\n"
                "    KeyCode key;\n"
                "    int repeats;\n"
                "\n"
                "    Stroke(KeyCode k, int n) { this.key = k; this.repeats = n; }\n"
                "}\n"
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        Stroke first = new Stroke(KeyCode.B, 2);\n"
                "        Stroke second = first;\n"
                "        second.key = KeyCode.C;\n"
                "        if (first.key != KeyCode.B) { return 0 - 1; }\n"
                "        if (second.key != KeyCode.C) { return 0 - 2; }\n"
                "        return first.repeats;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 2);
        }
    }
    {
        // A sequence of them holds them like any other value.
        ScriptValue value;
        if (RunProgram(ctx, "a-sequence-of-constants",
                std::string(kKeyCode) +
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        KeyCode[] pressed = new KeyCode[3];\n"
                "        pressed[0] = KeyCode.A;\n"
                "        pressed[2] = KeyCode.B;\n"
                "\n"
                "        int matches = 0;\n"
                "        foreach (KeyCode k in pressed) { if (k == KeyCode.Unknown) { matches += 1; } }\n"
                "        if (pressed[2] != KeyCode.B) { return 0 - 1; }\n"
                "        return matches;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            // Only the middle position was never written.
            TEST_CHECK(ctx, value.intValue == 1);
        }
    }

    // --- What a named constant is not -----------------------------------

    // Crossing between a set and a plain number is refused in both
    // directions: there is no way to write the conversion, so a number
    // and a constant never stand in for one another by accident.
    ExpectRejected(ctx, "a constant used as a number",
        std::string(kKeyCode) +
        "static class Program\n"
        "{\n"
        "    static int Main() { int n = KeyCode.B; return n; }\n"
        "}\n");

    ExpectRejected(ctx, "a number used as a constant",
        std::string(kKeyCode) +
        "static class Program\n"
        "{\n"
        "    static KeyCode Main() { KeyCode k = 10; return k; }\n"
        "}\n");

    ExpectRejected(ctx, "a constant compared with a number",
        std::string(kKeyCode) +
        "static class Program\n"
        "{\n"
        "    static bool Main() { return KeyCode.B == 10; }\n"
        "}\n");

    ExpectRejected(ctx, "a constant added to a number",
        std::string(kKeyCode) +
        "static class Program\n"
        "{\n"
        "    static int Main() { return KeyCode.B + 1; }\n"
        "}\n");

    ExpectRejected(ctx, "two sets compared with each other",
        std::string(kKeyCode) +
        "enum Button { Left, Right }\n"
        "static class Program\n"
        "{\n"
        "    static bool Main() { return KeyCode.A == Button.Left; }\n"
        "}\n");

    ExpectRejected(ctx, "a constant of another set assigned",
        std::string(kKeyCode) +
        "enum Button { Left, Right }\n"
        "static class Program\n"
        "{\n"
        "    static int Main() { KeyCode k = Button.Left; return 0; }\n"
        "}\n");

    ExpectRejected(ctx, "a constant that was never declared",
        std::string(kKeyCode) +
        "static class Program\n"
        "{\n"
        "    static KeyCode Main() { return KeyCode.Missing; }\n"
        "}\n");

    ExpectRejected(ctx, "a set declared twice over the same name",
        std::string(kKeyCode) +
        "enum Repeat { A, A }\n"
        "static class Program\n"
        "{\n"
        "    static int Main() { return 0; }\n"
        "}\n");

    ExpectRejected(ctx, "a set created with new",
        std::string(kKeyCode) +
        "static class Program\n"
        "{\n"
        "    static int Main() { KeyCode k = new KeyCode(); return 0; }\n"
        "}\n");

    ExpectRejected(ctx, "a set declared with type parameters",
        "enum Bad<T> { A, B }\n");

    ExpectRejected(ctx, "a set with nothing in it",
        "enum Bad { }\n");

    ExpectRejected(ctx, "a constant compared with null",
        std::string(kKeyCode) +
        "static class Program\n"
        "{\n"
        "    static bool Main() { return KeyCode.A == null; }\n"
        "}\n");
}
