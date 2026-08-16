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

// Every refusal has to say where the problem is. A message without a
// position leaves a reader to find it by hand, which is the whole of what
// a compiler is for.
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

const ClassInfo* FindClassInfo(const CompiledModule& module, const char* name)
{
    for (const ClassInfo& info : module.classes)
    {
        if (info.name == name) return &info;
    }
    return nullptr;
}

const FunctionInfo* FindFunction(const CompiledModule& module, const char* qualifiedName)
{
    for (const FunctionInfo& function : module.functions)
    {
        if (function.qualifiedName == qualifiedName) return &function;
    }
    return nullptr;
}

// Two positions and a scale in one value, so that copying one copies
// everything inside it and nothing outside it.
const char* const kPlacement =
    "struct Placement\n"
    "{\n"
    "    Vector3 position;\n"
    "    Vector3 scale;\n"
    "\n"
    "    float Spread() { return this.scale.x + this.scale.y + this.scale.z; }\n"
    "}\n";

} // namespace

void Test_Structs_Run(TestContext& ctx)
{
    {
        // Writing one component of one element must leave the rest of that
        // element and both neighbouring elements exactly as they were.
        ScriptValue value;
        if (RunProgram(ctx, "array-element-partial-write",
                "struct P { float x; float y; }\n"
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        P[] points = new P[3];\n"
                "        for (int i = 0; i < 3; i += 1)\n"
                "        {\n"
                "            P seed = new P();\n"
                "            seed.x = 10.0f;\n"
                "            seed.y = 20.0f;\n"
                "            points[i] = seed;\n"
                "        }\n"
                "        points[1].x = 7.0f;\n"
                "        return points[0].x + points[0].y * 100.0f\n"
                "             + points[1].x * 10000.0f + points[1].y * 1000000.0f\n"
                "             + points[2].x + points[2].y;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            // 10 + 2000 + 70000 + 20000000 + 10 + 20
            TEST_CHECK(ctx, value.type == ValueType::Float);
            TEST_CHECK(ctx, value.floatValue == 20072040.0f);
        }
    }
    {
        // Two levels down: touching the innermost component must not
        // disturb its siblings, its parent's other members, or the object.
        ScriptValue value;
        if (RunProgram(ctx, "nested-partial-write-through-two-levels",
                "struct P { float x; float y; }\n"
                "struct Pair { P a; P b; }\n"
                "class Holder { Pair pair; float tag; Holder() { this.tag = 9.0f; } }\n"
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        Holder h = new Holder();\n"
                "        Pair p = new Pair();\n"
                "        p.a.x = 1.0f; p.a.y = 2.0f; p.b.x = 3.0f; p.b.y = 4.0f;\n"
                "        h.pair = p;\n"
                "        h.pair.b.x = 30.0f;\n"
                "        return h.pair.a.x + h.pair.a.y * 10.0f + h.pair.b.x * 100.0f\n"
                "             + h.pair.b.y * 1000.0f + h.tag * 100000.0f;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            // 1 + 20 + 3000 + 4000 + 900000
            TEST_CHECK(ctx, value.floatValue == 907021.0f);
        }
    }
    {
        // A struct sitting between two reference fields is the alignment
        // case that matters: the collector's bitmap has to have a gap
        // exactly the width of the struct, or one of the two references
        // is missed.
        ScriptValue value;
        if (RunProgram(ctx, "reference-struct-reference-layout",
                "struct P { float x; float y; float z; }\n"
                "class Node { int v; Node(int n) { this.v = n; } int Get() { return this.v; } }\n"
                "class Mixed\n"
                "{\n"
                "    Node before;\n"
                "    P middle;\n"
                "    Node after;\n"
                "    Mixed(Node b, Node a) { this.before = b; this.after = a; }\n"
                "}\n"
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        Mixed m = new Mixed(new Node(3), new Node(5));\n"
                "        m.middle.x = 11.0f; m.middle.y = 22.0f; m.middle.z = 33.0f;\n"
                "        for (int i = 0; i < 400; i += 1) { Node junk = new Node(i); }\n"
                "        Gc.Collect();\n"
                "        float sum = m.before.Get() * 100 + m.after.Get();\n"
                "        return sum + m.middle.x + m.middle.y + m.middle.z;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            // 300 + 5 + 11 + 22 + 33 -- both references survived and the
            // struct in between kept its values.
            TEST_CHECK(ctx, value.type == ValueType::Float);
            TEST_CHECK(ctx, value.floatValue == 371.0f);
        }
    }
    {
        // Iterating an array of structs hands out copies: mutating the
        // loop variable must not write back into the array.
        ScriptValue value;
        if (RunProgram(ctx, "foreach-over-structs-yields-copies",
                "struct P { float x; }\n"
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        P[] points = new P[3];\n"
                "        points[0].x = 1.0f; points[1].x = 2.0f; points[2].x = 3.0f;\n"
                "        float total = 0.0f;\n"
                "        foreach (P p in points) { total += p.x; }\n"
                "        return total + points[0].x + points[1].x + points[2].x;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.floatValue == 12.0f); // 6 iterated + 6 still in place
        }
    }
    // --- Copying, which is the whole of what a value type promises ------

    {
        // Assignment copies. Writing through one name afterwards is
        // invisible through the other, in both directions.
        ScriptValue value;
        if (RunProgram(ctx, "assignment-copies",
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        Vector3 b = new Vector3(1.0f, 2.0f, 3.0f);\n"
                "        Vector3 a = b;\n"
                "        a.x = 5.0f;\n"
                "        float untouched = b.x;\n"
                "        b.y = 9.0f;\n"
                "        float stillMine = a.y;\n"
                "        return untouched * 100.0f + stillMine;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Float);
            TEST_CHECK(ctx, value.floatValue == 102.0f);
        }
    }
    {
        // Passing copies. A method that writes to what it was given
        // writes to its own copy of it.
        ScriptValue value;
        if (RunProgram(ctx, "passing-copies",
                "static class Program\n"
                "{\n"
                "    static float Bump(Vector3 v) { v.x = 99.0f; return v.x; }\n"
                "    static float Main()\n"
                "    {\n"
                "        Vector3 a = new Vector3(1.0f, 2.0f, 3.0f);\n"
                "        float inside = Bump(a);\n"
                "        return inside * 100.0f + a.x;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            // The method saw its change; the caller's value never did.
            TEST_CHECK(ctx, value.floatValue == 9901.0f);
        }
    }
    {
        // Answering copies. Two calls produce two values, and writing to
        // one leaves the other alone.
        ScriptValue value;
        if (RunProgram(ctx, "returning-copies",
                "static class Program\n"
                "{\n"
                "    static Vector3 Make() { return new Vector3(1.0f, 2.0f, 3.0f); }\n"
                "    static float Main()\n"
                "    {\n"
                "        Vector3 a = Make();\n"
                "        a.x = 7.0f;\n"
                "        Vector3 b = Make();\n"
                "        return a.x * 10.0f + b.x;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.floatValue == 71.0f);
        }
    }
    {
        // A method on a value type runs on its own copy: writing to
        // `this` inside one changes nothing the caller can see.
        ScriptValue value;
        if (RunProgram(ctx, "a-method-runs-on-its-own-copy",
                "struct Counter\n"
                "{\n"
                "    int n;\n"
                "    Counter(int start) { this.n = start; }\n"
                "    int BumpAndRead() { this.n = this.n + 1; return this.n; }\n"
                "}\n"
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        Counter c = new Counter(4);\n"
                "        int inside = c.BumpAndRead();\n"
                "        return inside * 100 + c.n;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 504);
        }
    }
    {
        // A field of a class holding a value type: reading it out gives a
        // copy, and writing to that copy leaves the field as it was.
        ScriptValue value;
        if (RunProgram(ctx, "a-field-read-out-is-a-copy",
                "class Holder\n"
                "{\n"
                "    Vector3 v;\n"
                "    int tag;\n"
                "    Holder(int t) { this.tag = t; }\n"
                "}\n"
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        Holder h = new Holder(5);\n"
                "        h.v.x = 3.0f;\n"
                "        Vector3 copy = h.v;\n"
                "        copy.x = 8.0f;\n"
                "        return h.v.x * 10.0f + copy.x + h.tag * 1.0f;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.floatValue == 43.0f);
        }
    }

    // --- Writing one field and nothing else -----------------------------

    {
        // A store into one field leaves the others exactly as they were,
        // in a local and in an object's field alike.
        ScriptValue value;
        if (RunProgram(ctx, "a-partial-write-disturbs-nothing",
                "class Holder { Vector3 v; Holder() { } }\n"
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        Vector3 v = new Vector3(1.0f, 2.0f, 3.0f);\n"
                "        v.x = 10.0f;\n"
                "        float local = v.x + v.y + v.z;\n"
                "\n"
                "        Holder h = new Holder();\n"
                "        h.v = new Vector3(4.0f, 5.0f, 6.0f);\n"
                "        h.v.y = 50.0f;\n"
                "        float held = h.v.x + h.v.y + h.v.z;\n"
                "        return local * 1000.0f + held;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            // 15 in the local, 60 in the field.
            TEST_CHECK(ctx, value.floatValue == 15060.0f);
        }
    }
    {
        // A value type inside a value type is copied whole, and one
        // field of the inner one is still reachable on its own.
        ScriptValue value;
        if (RunProgram(ctx, "a-value-inside-a-value",
                std::string(kPlacement) +
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        Placement a = new Placement();\n"
                "        a.position.x = 1.0f;\n"
                "        a.position.y = 2.0f;\n"
                "        a.scale.x = 3.0f;\n"
                "\n"
                "        Placement b = a;\n"
                "        b.position.x = 50.0f;\n"
                "\n"
                "        return a.position.x * 1000.0f + b.position.x * 10.0f + a.scale.x + b.position.y;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            // 1000 kept, 500 written into the copy, 3 and 2 carried over.
            TEST_CHECK(ctx, value.floatValue == 1505.0f);
        }
    }
    {
        // A method on the outer value reaches through to the inner one.
        ScriptValue value;
        if (RunProgram(ctx, "a-method-reaches-the-inner-value",
                std::string(kPlacement) +
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        Placement p = new Placement();\n"
                "        p.scale = new Vector3(1.0f, 2.0f, 4.0f);\n"
                "        return p.Spread();\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.floatValue == 7.0f);
        }
    }

    // --- Sequences of value types ---------------------------------------

    {
        // Writing one element leaves its neighbours alone, whether the
        // whole element is replaced or one of its fields is written.
        ScriptValue value;
        if (RunProgram(ctx, "elements-are-independent",
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        Vector3[] points = new Vector3[4];\n"
                "        for (int i = 0; i < 4; i += 1) { points[i] = new Vector3(i * 1.0f, 1.0f, 2.0f); }\n"
                "\n"
                "        points[2] = new Vector3(9.0f, 8.0f, 7.0f);\n"
                "        if (points[1].x != 1.0f) { return 0.0f - 1.0f; }\n"
                "        if (points[3].x != 3.0f) { return 0.0f - 2.0f; }\n"
                "\n"
                "        points[2].x = 5.0f;\n"
                "        if (points[2].y != 8.0f) { return 0.0f - 3.0f; }\n"
                "        if (points[1].y != 1.0f) { return 0.0f - 4.0f; }\n"
                "        if (points[3].z != 2.0f) { return 0.0f - 5.0f; }\n"
                "\n"
                "        return points[2].x * 100.0f + points.Length * 1.0f;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.floatValue == 504.0f);
        }
    }
    {
        // A sequence of value types is walked like any other, and each
        // turn hands over a copy of the element.
        ScriptValue value;
        if (RunProgram(ctx, "walking-a-sequence-of-values",
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        Vector3[] points = new Vector3[3];\n"
                "        points[0] = new Vector3(1.0f, 0.0f, 0.0f);\n"
                "        points[1] = new Vector3(0.0f, 2.0f, 0.0f);\n"
                "        points[2] = new Vector3(0.0f, 0.0f, 3.0f);\n"
                "\n"
                "        float total = 0.0f;\n"
                "        foreach (Vector3 p in points) { total += p.x + p.y + p.z; }\n"
                "        return total;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.floatValue == 6.0f);
        }
    }

    {
        // A value type handed to a declaration with type parameters: the
        // copy made for it holds the value inline, so its field area is
        // as wide as the value and nothing in it is followed.
        ScriptValue value;
        if (RunProgram(ctx, "a-value-type-as-a-type-argument",
                "class Box<T>\n"
                "{\n"
                "    T value;\n"
                "    Box(T v) { this.value = v; }\n"
                "    T Get() { return this.value; }\n"
                "    void Set(T v) { this.value = v; }\n"
                "}\n"
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        Box<Vector3> box = new Box<Vector3>(new Vector3(1.0f, 2.0f, 3.0f));\n"
                "        Vector3 taken = box.Get();\n"
                "        taken.x = 9.0f;\n"
                "        if (box.Get().x != 1.0f) { return 0.0f - 1.0f; }\n"
                "\n"
                "        box.Set(new Vector3(4.0f, 5.0f, 6.0f));\n"
                "        return box.Get().x + box.Get().z + taken.x;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.floatValue == 19.0f);
        }
    }

    // --- What the built-in value types compute --------------------------

    {
        ScriptValue value;
        if (RunProgram(ctx, "the-built-in-vector-arithmetic",
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        Vector3 a = new Vector3(1.0f, 2.0f, 2.0f);\n"
                "        if (a.Length() != 3.0f) { return 0.0f - 1.0f; }\n"
                "        if (a.LengthSquared() != 9.0f) { return 0.0f - 2.0f; }\n"
                "\n"
                "        Vector3 b = new Vector3(3.0f, 0.0f, 0.0f);\n"
                "        if (a.Add(b).x != 4.0f) { return 0.0f - 3.0f; }\n"
                "        if (a.Subtract(b).x != 0.0f - 2.0f) { return 0.0f - 4.0f; }\n"
                "        if (a.Scale(2.0f).y != 4.0f) { return 0.0f - 5.0f; }\n"
                "        if (a.Dot(b) != 3.0f) { return 0.0f - 6.0f; }\n"
                "\n"
                "        Vector3 cross = new Vector3(1.0f, 0.0f, 0.0f).Cross(new Vector3(0.0f, 1.0f, 0.0f));\n"
                "        if (cross.z != 1.0f) { return 0.0f - 7.0f; }\n"
                "        if (cross.x != 0.0f) { return 0.0f - 8.0f; }\n"
                "\n"
                "        Vector3 unit = new Vector3(0.0f, 5.0f, 0.0f).Normalized();\n"
                "        if (unit.y != 1.0f) { return 0.0f - 9.0f; }\n"
                "\n"
                "        Vector3 nothing = new Vector3(0.0f, 0.0f, 0.0f).Normalized();\n"
                "        if (nothing.x != 0.0f) { return 0.0f - 10.0f; }\n"
                "\n"
                "        Vector2 flat = new Vector2(3.0f, 4.0f);\n"
                "        if (flat.Length() != 5.0f) { return 0.0f - 11.0f; }\n"
                "\n"
                "        Vector4 wide = new Vector4(1.0f, 2.0f, 3.0f, 4.0f);\n"
                "        if (wide.AsVector3().z != 3.0f) { return 0.0f - 12.0f; }\n"
                "\n"
                "        Color faded = Color.White().WithAlpha(0.5f);\n"
                "        if (faded.a != 0.5f) { return 0.0f - 13.0f; }\n"
                "        if (Color.FromBytes(255, 0, 0, 255).r != 1.0f) { return 0.0f - 14.0f; }\n"
                "\n"
                "        return a.Distance(new Vector3(1.0f, 2.0f, 2.0f));\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.floatValue == 0.0f);
        }
    }
    {
        // A rotation of a quarter turn about z takes the x axis to the y
        // axis. The numbers involved are not exact, so what is checked is
        // that the answer is where it should be to within a hair.
        ScriptValue value;
        if (RunProgram(ctx, "the-built-in-rotation",
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        Quaternion identity = Quaternion.Identity();\n"
                "        Vector3 kept = identity.Rotate(new Vector3(1.0f, 2.0f, 3.0f));\n"
                "        if (kept.y != 2.0f) { return 0.0f - 1.0f; }\n"
                "\n"
                "        Quaternion quarter = Quaternion.FromAxisAngle(new Vector3(0.0f, 0.0f, 1.0f), 1.5707963f);\n"
                "        Vector3 turned = quarter.Rotate(new Vector3(1.0f, 0.0f, 0.0f));\n"
                "        if (turned.y < 0.999f) { return 0.0f - 2.0f; }\n"
                "        if (turned.x > 0.001f) { return 0.0f - 3.0f; }\n"
                "\n"
                "        Quaternion half = quarter.Multiply(quarter);\n"
                "        Vector3 flipped = half.Rotate(new Vector3(1.0f, 0.0f, 0.0f));\n"
                "        if (flipped.x > 0.0f - 0.999f) { return 0.0f - 4.0f; }\n"
                "\n"
                "        if (quarter.Multiply(quarter.Conjugate()).w < 0.999f) { return 0.0f - 5.0f; }\n"
                "        return quarter.Length();\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.floatValue > 0.999f && value.floatValue < 1.001f);
        }
    }

    // --- What the module records about a value type ---------------------

    {
        DiagnosticList diagnostics;
        CompileOptions options;
        options.fileName = "value-types-carry-their-own-layout";

        auto compiled = Compile(
            std::string(kPlacement) +
            "class Node { Vector3 offset; Node next; int tag; Node() { } }\n"
            "static class Program\n"
            "{\n"
            "    static int Main()\n"
            "    {\n"
            "        Vector3[] points = new Vector3[2];\n"
            "        Placement p = new Placement();\n"
            "        Node n = new Node();\n"
            "        if (p.position.x != 0.0f) { return 0 - 1; }\n"
            "        return points.Length + n.tag;\n"
            "    }\n"
            "}\n",
            options, diagnostics);

        if (!compiled.IsOk()) ReportDiagnostics(diagnostics);
        TEST_CHECK(ctx, compiled.IsOk());

        if (compiled.IsOk())
        {
            const CompiledModule& module = compiled.Value();

            const ClassInfo* vector = FindClassInfo(module, "Vector3");
            TEST_CHECK(ctx, vector != nullptr);
            if (vector)
            {
                TEST_CHECK(ctx, vector->isStruct && !vector->isArray && !vector->isInterface);
                TEST_CHECK(ctx, vector->fieldSlotCount == 3);
                // Nothing inside a value type is ever followed.
                for (u32 slot = 0; slot < 3; ++slot)
                    TEST_CHECK(ctx, !IsReferenceBitSet(vector->fieldReferenceBits, slot));
            }

            // One value type inside another is laid out flat: six slots,
            // not two.
            const ClassInfo* placement = FindClassInfo(module, "Placement");
            TEST_CHECK(ctx, placement != nullptr);
            if (placement) TEST_CHECK(ctx, placement->isStruct && placement->fieldSlotCount == 6);

            // A class holding one has its own fields pushed along by its
            // width, and marks only the field that names an object.
            const ClassInfo* node = FindClassInfo(module, "Node");
            TEST_CHECK(ctx, node != nullptr);
            if (node)
            {
                TEST_CHECK(ctx, node->fieldSlotCount == 5);
                TEST_CHECK(ctx, !IsReferenceBitSet(node->fieldReferenceBits, 0));
                TEST_CHECK(ctx, !IsReferenceBitSet(node->fieldReferenceBits, 1));
                TEST_CHECK(ctx, !IsReferenceBitSet(node->fieldReferenceBits, 2));
                TEST_CHECK(ctx, IsReferenceBitSet(node->fieldReferenceBits, 3));
                TEST_CHECK(ctx, !IsReferenceBitSet(node->fieldReferenceBits, 4));
            }

            // A sequence of them says how wide one element is, and that
            // none of them names anything.
            const ClassInfo* points = FindClassInfo(module, "Vector3[]");
            TEST_CHECK(ctx, points != nullptr);
            if (points)
            {
                TEST_CHECK(ctx, points->isArray && points->elementSlotCount == 3);
                TEST_CHECK(ctx, !points->elementIsReference);
            }

            // A method taking one takes its whole width, and a
            // constructor of one answers with what it built.
            const FunctionInfo* add = FindFunction(module, "Vector3.Add");
            TEST_CHECK(ctx, add != nullptr);
            if (add)
            {
                TEST_CHECK(ctx, add->receiverSlotCount == 3 && add->receiverIsValue);
                TEST_CHECK(ctx, add->parameterSlotCount == 3 && add->parameterTypes.size() == 1);
                TEST_CHECK(ctx, add->returnSlotCount == 3);
            }

            const FunctionInfo* constructor = FindFunction(module, "Vector3.Vector3");
            TEST_CHECK(ctx, constructor != nullptr);
            if (constructor)
            {
                TEST_CHECK(ctx, constructor->returnSlotCount == 3);
                TEST_CHECK(ctx, constructor->returnType == ValueType::Struct);
            }
        }
    }

    // --- What a value type may not be -----------------------------------

    ExpectRejected(ctx, "text in a value type",
        "struct Bad\n"
        "{\n"
        "    int n;\n"
        "    string name;\n"
        "}\n");

    ExpectRejected(ctx, "a reference in a value type",
        "class Node { int n; Node() { } }\n"
        "struct Bad\n"
        "{\n"
        "    int n;\n"
        "    Node held;\n"
        "}\n");

    ExpectRejected(ctx, "a sequence in a value type",
        "struct Bad\n"
        "{\n"
        "    int n;\n"
        "    int[] numbers;\n"
        "}\n");

    ExpectRejected(ctx, "a value type built on something",
        "class Base { int n; Base() { } }\n"
        "struct Bad : Base\n"
        "{\n"
        "    int n;\n"
        "}\n");

    ExpectRejected(ctx, "a value type answering an interface",
        "interface Named { int Tag(); }\n"
        "struct Bad : Named\n"
        "{\n"
        "    int n;\n"
        "    int Tag() { return this.n; }\n"
        "}\n");

    ExpectRejected(ctx, "a method on a value type declared virtual",
        "struct Bad\n"
        "{\n"
        "    int n;\n"
        "    virtual int Read() { return this.n; }\n"
        "}\n");

    ExpectRejected(ctx, "a method on a value type declared override",
        "struct Bad\n"
        "{\n"
        "    int n;\n"
        "    override int Read() { return this.n; }\n"
        "}\n");

    ExpectRejected(ctx, "a value type compared with null",
        "static class Program\n"
        "{\n"
        "    static bool Main()\n"
        "    {\n"
        "        Vector3 v = new Vector3(1.0f, 2.0f, 3.0f);\n"
        "        return v == null;\n"
        "    }\n"
        "}\n");

    ExpectRejected(ctx, "a value type assigned null",
        "static class Program\n"
        "{\n"
        "    static float Main()\n"
        "    {\n"
        "        Vector3 v = null;\n"
        "        return v.x;\n"
        "    }\n"
        "}\n");

    ExpectRejected(ctx, "a value type declared with type parameters",
        "struct Bad<T>\n"
        "{\n"
        "    int n;\n"
        "}\n");

    ExpectRejected(ctx, "a value type that holds itself",
        "struct Bad\n"
        "{\n"
        "    int n;\n"
        "    Bad inner;\n"
        "}\n");

    ExpectRejected(ctx, "a value type with no fields",
        "struct Bad\n"
        "{\n"
        "    int Read() { return 0; }\n"
        "}\n");

    ExpectRejected(ctx, "one value type assigned another",
        "static class Program\n"
        "{\n"
        "    static float Main()\n"
        "    {\n"
        "        Vector3 v = new Vector2(1.0f, 2.0f);\n"
        "        return v.x;\n"
        "    }\n"
        "}\n");

    ExpectRejected(ctx, "writing to a field of an answer",
        "static class Program\n"
        "{\n"
        "    static Vector3 Make() { return new Vector3(1.0f, 2.0f, 3.0f); }\n"
        "    static float Main()\n"
        "    {\n"
        "        Make().x = 5.0f;\n"
        "        return 0.0f;\n"
        "    }\n"
        "}\n");
}
