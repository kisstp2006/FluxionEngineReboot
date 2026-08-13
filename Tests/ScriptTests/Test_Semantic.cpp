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

    ExpectRejected(ctx, "override without a virtual base",
        "class Base\n"
        "{\n"
        "    float Area() { return 1.0f; }\n"
        "}\n"
        "class Derived : Base\n"
        "{\n"
        "    override float Area() { return 2.0f; }\n"
        "}\n");

    ExpectRejected(ctx, "override of a method no base class declares",
        "class Base { }\n"
        "class Derived : Base\n"
        "{\n"
        "    override float Area() { return 2.0f; }\n"
        "}\n");

    ExpectRejected(ctx, "override whose signature does not match",
        "class Base\n"
        "{\n"
        "    virtual float Area(int n) { return 1.0f; }\n"
        "}\n"
        "class Derived : Base\n"
        "{\n"
        "    override float Area(float n) { return 2.0f; }\n"
        "}\n");

    ExpectRejected(ctx, "method hiding a virtual base method",
        "class Base\n"
        "{\n"
        "    virtual float Area() { return 1.0f; }\n"
        "}\n"
        "class Derived : Base\n"
        "{\n"
        "    float Area() { return 2.0f; }\n"
        "}\n");

    ExpectRejected(ctx, "class missing a member its interface declares",
        "interface IShape { float Area(); }\n"
        "class Blob : IShape\n"
        "{\n"
        "    float Size() { return 1.0f; }\n"
        "}\n");

    ExpectRejected(ctx, "interface member implemented with the wrong signature",
        "interface IShape { float Area(); }\n"
        "class Blob : IShape\n"
        "{\n"
        "    int Area() { return 1; }\n"
        "}\n");

    ExpectRejected(ctx, "static class created with new",
        "static class Helper { static int One() { return 1; } }\n"
        "static class P\n"
        "{\n"
        "    static void M() { var h = new Helper(); }\n"
        "}\n");

    ExpectRejected(ctx, "interface created with new",
        "interface IShape { float Area(); }\n"
        "static class P\n"
        "{\n"
        "    static void M() { var s = new IShape(); }\n"
        "}\n");

    ExpectRejected(ctx, "base reference assigned to a derived variable",
        "class Base { }\n"
        "class Derived : Base { }\n"
        "static class P\n"
        "{\n"
        "    static void M() { Base b = new Base(); Derived d = b; }\n"
        "}\n");

    ExpectRejected(ctx, "unrelated reference assigned across the hierarchy",
        "class Left { }\n"
        "class Right { }\n"
        "static class P\n"
        "{\n"
        "    static void M() { Left l = new Left(); Right r = l; }\n"
        "}\n");

    ExpectRejected(ctx, "base constructor arguments left out",
        "class Base\n"
        "{\n"
        "    float scale;\n"
        "    Base(float s) { this.scale = s; }\n"
        "}\n"
        "class Derived : Base\n"
        "{\n"
        "    Derived() { }\n"
        "}\n");

    ExpectRejected(ctx, "base constructor left out entirely",
        "class Base\n"
        "{\n"
        "    float scale;\n"
        "    Base(float s) { this.scale = s; }\n"
        "}\n"
        "class Derived : Base\n"
        "{\n"
        "    float extra;\n"
        "}\n");

    ExpectRejected(ctx, "base constructor given the wrong argument type",
        "class Base\n"
        "{\n"
        "    float scale;\n"
        "    Base(float s) { this.scale = s; }\n"
        "}\n"
        "class Derived : Base\n"
        "{\n"
        "    Derived() : base(\"text\") { }\n"
        "}\n");

    ExpectRejected(ctx, "instance method called from a static method without an instance",
        "class Thing\n"
        "{\n"
        "    float Value() { return 1.0f; }\n"
        "    static float M() { return Value(); }\n"
        "}\n");

    ExpectRejected(ctx, "instance field used from a static method",
        "class Thing\n"
        "{\n"
        "    float value;\n"
        "    static float M() { return value; }\n"
        "}\n");

    ExpectRejected(ctx, "'this' used in a static method",
        "class Thing\n"
        "{\n"
        "    float value;\n"
        "    static float M() { return this.value; }\n"
        "}\n");

    ExpectRejected(ctx, "static class used as a base class",
        "static class Helper { static int One() { return 1; } }\n"
        "class Derived : Helper { }\n");

    ExpectRejected(ctx, "class inheriting from itself through its base list",
        "class Loop : Loop { }\n");

    ExpectRejected(ctx, "field declared by an interface",
        "interface IShape { float Area(); }\n"
        "static class P { static void M() { } }\n"
        "interface IBad { float size; }\n");

    ExpectRejected(ctx, "field of a type that is not declared",
        "class Holder { Missing thing; }\n");

    ExpectRejected(ctx, "reference compared with a number",
        "class Thing { }\n"
        "static class P\n"
        "{\n"
        "    static void M() { Thing t = new Thing(); bool b = t == 1; }\n"
        "}\n");

    ExpectRejected(ctx, "field read on something that is not a reference",
        "static class P\n"
        "{\n"
        "    static void M() { int x = 1; int y = x.field; }\n"
        "}\n");

    ExpectRejected(ctx, "field that does not exist on the class",
        "class Thing { float value; }\n"
        "static class P\n"
        "{\n"
        "    static void M() { Thing t = new Thing(); float f = t.missing; }\n"
        "}\n");

    ExpectRejected(ctx, "constructor arguments that do not match",
        "class Thing { float value; Thing(float v) { this.value = v; } }\n"
        "static class P\n"
        "{\n"
        "    static void M() { Thing t = new Thing(); }\n"
        "}\n");

    ExpectRejected(ctx, "inferred declaration from null",
        "static class P\n"
        "{\n"
        "    static void M() { var x = null; }\n"
        "}\n");

    {
        // The whole object surface, accepted: a base list mixing a class
        // and an interface, a chained constructor, dispatch through both
        // a base-typed and an interface-typed reference, and null.
        DiagnosticList diagnostics;
        const bool accepted = AnalyzeSource(
            "interface IShape\n"
            "{\n"
            "    float Area();\n"
            "}\n"
            "class Shape : IShape\n"
            "{\n"
            "    float scale;\n"
            "    Shape(float s) { this.scale = s; }\n"
            "    virtual float Area() { return this.scale; }\n"
            "    float Scaled(float k) { return this.Area() * k; }\n"
            "}\n"
            "class Circle : Shape\n"
            "{\n"
            "    float radius;\n"
            "    Circle(float r) : base(2.0f) { this.radius = r; }\n"
            "    override float Area() { return this.radius * this.radius * this.scale; }\n"
            "}\n"
            "static class Program\n"
            "{\n"
            "    static float Main()\n"
            "    {\n"
            "        Shape s = new Circle(2.0f);\n"
            "        IShape viewed = s;\n"
            "        if (s == null) { return 0.0f; }\n"
            "        return s.Area() + s.Scaled(2.0f) + viewed.Area();\n"
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
    {
        // A class with no constructor of its own gets one, and chaining
        // to a base that needs nothing is implicit.
        DiagnosticList diagnostics;
        const bool accepted = AnalyzeSource(
            "class Base { int count; }\n"
            "class Derived : Base { float extra; }\n"
            "static class P\n"
            "{\n"
            "    static int M() { Derived d = new Derived(); return d.count; }\n"
            "}\n",
            diagnostics);
        TEST_CHECK(ctx, accepted);
    }
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
