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

// Compiles the source and runs one named method, handing back what it
// returned.
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

// Runs a method that is expected to stop with a fault instead of
// producing a value.
void ExpectFault(TestContext& ctx, const char* label, const std::string& source, const char* entry)
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
        return;
    }

    Vm* vm = CreateVm(compiled.Value(), diagnostics);
    if (!vm)
    {
        std::fprintf(stderr, "  FAIL: '%s' did not load\n", label);
        ReportDiagnostics(diagnostics);
        ctx.failures++;
        return;
    }

    auto result = Invoke(vm, entry);
    if (result.IsOk())
    {
        std::fprintf(stderr, "  FAIL: expected '%s' to fault\n", label);
        ctx.failures++;
    }
    else
    {
        TEST_CHECK(ctx, result.Status().message != nullptr);
    }
    DestroyVm(vm);
}

const FunctionInfo* FindFunction(const CompiledModule& module, const char* qualifiedName)
{
    for (const FunctionInfo& function : module.functions)
    {
        if (function.qualifiedName == qualifiedName) return &function;
    }
    return nullptr;
}

u32 CountOpcode(const CompiledModule& module, const FunctionInfo& function, OpCode op)
{
    u32 count = 0;
    for (u32 i = 0; i < function.codeLength; ++i)
    {
        if (module.code[function.codeOffset + i].op == op) ++count;
    }
    return count;
}

// The class hierarchy most of the assertions below are made against.
const char* const kShapes =
    "interface IShape\n"
    "{\n"
    "    float Area();\n"
    "}\n"
    "\n"
    "class Shape : IShape\n"
    "{\n"
    "    float scale;\n"
    "\n"
    "    Shape(float s) { this.scale = s; }\n"
    "\n"
    "    virtual float Area() { return this.scale; }\n"
    "    float Scaled(float k) { return this.Area() * k; }\n"
    "    float Scale() { return scale; }\n"
    "}\n"
    "\n"
    "class Circle : Shape\n"
    "{\n"
    "    float radius;\n"
    "\n"
    "    Circle(float r) : base(2.0f) { this.radius = r; }\n"
    "\n"
    "    override float Area() { return this.radius * this.radius * this.scale; }\n"
    "}\n";

} // namespace

void Test_Objects_Run(TestContext& ctx)
{
    {
        // Three levels deep, every level overriding: a reference typed as
        // the root must still reach the most-derived body.
        ScriptValue value;
        if (RunProgram(ctx, "three-level-override",
                "class A { virtual int Rank() { return 1; } }\n"
                "class B : A { override int Rank() { return 2; } }\n"
                "class C : B { override int Rank() { return 3; } }\n"
                "static class Program\n"
                "{\n"
                "    static int Main() { A a = new C(); B b = new C(); return a.Rank() * 10 + b.Rank(); }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 33);
        }
    }
    {
        // A level that does not override inherits the nearest ancestor's
        // body rather than falling back to the root's.
        ScriptValue value;
        if (RunProgram(ctx, "override-gap-inherits-nearest",
                "class A { virtual int Rank() { return 1; } }\n"
                "class B : A { override int Rank() { return 2; } }\n"
                "class C : B { }\n"
                "static class Program { static int Main() { A a = new C(); return a.Rank(); } }\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 2);
        }
    }
    {
        // An interface declared on the base is still satisfied, and still
        // dispatches, when the instance is a derived class.
        ScriptValue value;
        if (RunProgram(ctx, "interface-through-derived",
                "interface ICounter { int Count(); }\n"
                "class Base : ICounter { virtual int Count() { return 5; } }\n"
                "class Derived : Base { override int Count() { return 9; } }\n"
                "static class Program { static int Main() { ICounter c = new Derived(); return c.Count(); } }\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 9);
        }
    }
    {
        // A base-class field is readable and writable from a derived
        // method, and both classes' fields coexist on one instance.
        ScriptValue value;
        if (RunProgram(ctx, "inherited-field-access",
                "class Base { int a; Base() { this.a = 3; } }\n"
                "class Derived : Base { int b; Derived() { this.b = 4; this.a = this.a + 10; } int Sum() { return this.a + this.b; } }\n"
                "static class Program { static int Main() { Derived d = new Derived(); return d.Sum(); } }\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 17); // (3+10) + 4
        }
    }
    {
        // Reference fields start out null, and a null one reports a fault
        // instead of crashing when it is walked into.
        ScriptValue value;
        if (RunProgram(ctx, "reference-field-defaults-null",
                "class Node { Node next; int v; Node(int x) { this.v = x; } }\n"
                "static class Program { static bool Main() { Node n = new Node(1); return n.next == null; } }\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Bool);
            TEST_CHECK(ctx, value.boolValue);
        }
    }
    {
        // Every field starts at the zero of its own type, with no
        // initialization written anywhere.
        ScriptValue value;
        if (RunProgram(ctx, "field-defaults",
                "class Defaults\n"
                "{\n"
                "    int i;\n"
                "    float f;\n"
                "    bool b;\n"
                "    string s;\n"
                "    Defaults other;\n"
                "\n"
                "    int GetInt() { return this.i; }\n"
                "    float GetFloat() { return this.f; }\n"
                "    bool GetBool() { return this.b; }\n"
                "    string GetString() { return this.s; }\n"
                "    bool OtherIsNull() { return this.other == null; }\n"
                "}\n"
                "static class Program\n"
                "{\n"
                "    static bool Main()\n"
                "    {\n"
                "        Defaults d = new Defaults();\n"
                "        return d.GetInt() == 0 && d.GetFloat() == 0.0f && !d.GetBool() && d.GetString() == \"\" && d.OtherIsNull();\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Bool);
            TEST_CHECK(ctx, value.boolValue);
        }
    }
    {
        // A field written through one method is visible to another, and a
        // bare name resolves to the field of the surrounding instance.
        ScriptValue value;
        if (RunProgram(ctx, "field-round-trip",
                "class Counter\n"
                "{\n"
                "    int count;\n"
                "\n"
                "    void Add(int n) { this.count = this.count + n; }\n"
                "    void Bump() { count += 5; }\n"
                "    int Read() { return count; }\n"
                "}\n"
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        Counter c = new Counter();\n"
                "        c.Add(3);\n"
                "        c.Add(4);\n"
                "        c.Bump();\n"
                "        return c.Read();\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Int);
            TEST_CHECK(ctx, value.intValue == 12);
        }
    }
    {
        // The base constructor runs first, so both levels of fields hold
        // what their own constructor put there.
        ScriptValue value;
        if (RunProgram(ctx, "base-constructor-runs-first",
                std::string(kShapes) +
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        Circle c = new Circle(3.0f);\n"
                "        return c.Scale() * 100.0f + c.radius;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Float);
            TEST_CHECK(ctx, value.floatValue == 203.0f); // scale 2 from the base, radius 3 from the derived
        }
    }
    {
        // The assertion this whole hierarchy exists for: a reference
        // declared as the base runs the derived implementation.
        ScriptValue value;
        if (RunProgram(ctx, "virtual-through-base-reference",
                std::string(kShapes) +
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        Shape s = new Circle(3.0f);\n"
                "        return s.Area();\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Float);
            TEST_CHECK(ctx, value.floatValue == 18.0f); // 3 * 3 * 2
        }
    }
    {
        // The same reference held as the class it was created as gives
        // the same answer, which is what makes the previous case about
        // dispatch and not about the value.
        ScriptValue value;
        if (RunProgram(ctx, "virtual-through-own-reference",
                std::string(kShapes) +
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        Circle c = new Circle(3.0f);\n"
                "        Shape s = c;\n"
                "        return s.Area() - c.Area();\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.floatValue == 0.0f);
        }
    }
    {
        // A method that is not dispatched on runs the one body there is,
        // reached through a base-typed reference -- and the virtual call
        // it makes on the way still finds the derived one.
        ScriptValue value;
        if (RunProgram(ctx, "non-virtual-through-base-reference",
                std::string(kShapes) +
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        Shape s = new Circle(3.0f);\n"
                "        Shape plain = new Shape(4.0f);\n"
                "        return s.Scaled(2.0f) + plain.Scaled(2.0f);\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.floatValue == 44.0f); // 18*2 + 4*2
        }
    }
    {
        // ... and the two call kinds really are different instructions:
        // the non-virtual one is bound where it is written, the
        // dispatched one is not.
        DiagnosticList diagnostics;
        CompileOptions options;
        options.fileName = "call-binding";
        auto compiled = Compile(
            std::string(kShapes) +
            "static class Program\n"
            "{\n"
            "    static float Main()\n"
            "    {\n"
            "        Shape s = new Circle(3.0f);\n"
            "        return s.Scaled(2.0f);\n"
            "    }\n"
            "}\n",
            options, diagnostics);
        TEST_CHECK(ctx, compiled.IsOk());
        if (compiled.IsOk())
        {
            const CompiledModule& module = compiled.Value();

            const FunctionInfo* main = FindFunction(module, "Program.Main");
            TEST_CHECK(ctx, main != nullptr);
            if (main)
            {
                TEST_CHECK(ctx, CountOpcode(module, *main, OpCode::CallVirtual) == 0);
                TEST_CHECK(ctx, CountOpcode(module, *main, OpCode::NewObject) == 1);
            }

            const FunctionInfo* scaled = FindFunction(module, "Shape.Scaled");
            TEST_CHECK(ctx, scaled != nullptr);
            if (scaled) TEST_CHECK(ctx, CountOpcode(module, *scaled, OpCode::CallVirtual) == 1);

            // The override shares its base's slot rather than taking a
            // new one, which is what keeps the base's call sites working.
            const FunctionInfo* baseArea = FindFunction(module, "Shape.Area");
            const FunctionInfo* derivedArea = FindFunction(module, "Circle.Area");
            TEST_CHECK(ctx, baseArea != nullptr && derivedArea != nullptr);
            if (baseArea && derivedArea)
            {
                TEST_CHECK(ctx, baseArea->vtableSlot != kNoVtableSlot);
                TEST_CHECK(ctx, baseArea->vtableSlot == derivedArea->vtableSlot);
            }
        }
    }
    {
        // A reference held as an interface reaches the implementation of
        // whichever class the object actually is.
        ScriptValue value;
        if (RunProgram(ctx, "interface-dispatch",
                std::string(kShapes) +
                "static class Program\n"
                "{\n"
                "    static float Sum(IShape a, IShape b) { return a.Area() + b.Area(); }\n"
                "    static float Main()\n"
                "    {\n"
                "        IShape circle = new Circle(3.0f);\n"
                "        IShape plain = new Shape(5.0f);\n"
                "        return Sum(circle, plain);\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.floatValue == 23.0f); // 18 + 5
        }
    }
    {
        // Two allocations are two objects however equal their contents;
        // a copied reference is the same object.
        ScriptValue value;
        if (RunProgram(ctx, "reference-identity",
                std::string(kShapes) +
                "static class Program\n"
                "{\n"
                "    static bool Main()\n"
                "    {\n"
                "        Circle a = new Circle(1.0f);\n"
                "        Circle b = new Circle(1.0f);\n"
                "        Circle copy = a;\n"
                "        return a != b && copy == a && !(a == b);\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.boolValue);
        }
    }
    {
        // `null` goes into any reference, compares with any reference,
        // and is what a reference holds before anything is put in it.
        ScriptValue value;
        if (RunProgram(ctx, "null-handling",
                std::string(kShapes) +
                "static class Program\n"
                "{\n"
                "    static bool Main()\n"
                "    {\n"
                "        Shape s = null;\n"
                "        Shape untouched;\n"
                "        bool startsNull = s == null && untouched == null;\n"
                "        s = new Circle(1.0f);\n"
                "        bool thenSet = s != null;\n"
                "        s = null;\n"
                "        return startsNull && thenSet && s == null;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.boolValue);
        }
    }
    {
        // A method that returns a reference can return null, and the
        // caller can tell.
        ScriptValue value;
        if (RunProgram(ctx, "null-return",
                std::string(kShapes) +
                "static class Program\n"
                "{\n"
                "    static Shape Pick(bool want) { if (want) { return new Shape(1.0f); } return null; }\n"
                "    static bool Main() { return Pick(true) != null && Pick(false) == null; }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.boolValue);
        }
    }

    // Reaching through a reference that holds nothing is reported, not
    // survived by accident.
    ExpectFault(ctx, "null-field-read",
        std::string(kShapes) +
        "static class Program\n"
        "{\n"
        "    static float Main() { Shape s = null; return s.scale; }\n"
        "}\n",
        "Program.Main");

    ExpectFault(ctx, "null-field-write",
        std::string(kShapes) +
        "static class Program\n"
        "{\n"
        "    static float Main() { Shape s = null; s.scale = 1.0f; return 0.0f; }\n"
        "}\n",
        "Program.Main");

    ExpectFault(ctx, "null-virtual-call",
        std::string(kShapes) +
        "static class Program\n"
        "{\n"
        "    static float Main() { Shape s = null; return s.Area(); }\n"
        "}\n",
        "Program.Main");

    ExpectFault(ctx, "null-non-virtual-call",
        std::string(kShapes) +
        "static class Program\n"
        "{\n"
        "    static float Main() { Shape s = null; return s.Scaled(2.0f); }\n"
        "}\n",
        "Program.Main");

    ExpectFault(ctx, "null-interface-call",
        std::string(kShapes) +
        "static class Program\n"
        "{\n"
        "    static float Main() { IShape s = null; return s.Area(); }\n"
        "}\n",
        "Program.Main");
}
