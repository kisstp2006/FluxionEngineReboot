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

bool CompileProgram(TestContext& ctx, const char* label, const std::string& source, CompiledModule& outModule)
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

    outModule = compiled.Value();
    return true;
}

bool RunProgram(TestContext& ctx, const char* label, const std::string& source, const char* entry, ScriptValue& outValue)
{
    CompiledModule module;
    if (!CompileProgram(ctx, label, source, module)) return false;

    DiagnosticList diagnostics;
    Vm* vm = CreateVm(module, diagnostics);
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

const Diagnostic* FirstError(const DiagnosticList& diagnostics)
{
    for (const Diagnostic& entry : diagnostics.entries)
    {
        if (entry.severity == DiagnosticSeverity::Error) return &entry;
    }
    return nullptr;
}

// Every rejection must arrive with a usable position, and it must be a
// position in the source that was handed in -- a message pointing at
// something the caller never wrote is worse than no message.
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

    const Diagnostic* error = FirstError(diagnostics);
    if (!error)
    {
        std::fprintf(stderr, "  FAIL: '%s' was rejected without a diagnostic\n", label);
        ctx.failures++;
        return;
    }

    if (error->location.line == 0 || error->location.column == 0 || error->location.file != label)
    {
        std::fprintf(stderr, "  FAIL: '%s' reported at %s:%u:%u: %s\n", label,
            error->location.file.c_str(), error->location.line, error->location.column, error->message.c_str());
        ctx.failures++;
    }
}

const ClassInfo* FindClass(const CompiledModule& module, const char* name)
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

const char* const kBox =
    "class Box<T>\n"
    "{\n"
    "    T value;\n"
    "\n"
    "    Box(T v) { this.value = v; }\n"
    "\n"
    "    T Get() { return this.value; }\n"
    "    void Set(T v) { this.value = v; }\n"
    "}\n";

} // namespace

void Test_Generics_Run(TestContext& ctx)
{
    {
        // A type argument of `int` means the field holds an actual int:
        // the value goes in and comes back out unchanged, including one
        // that no smaller representation could carry.
        ScriptValue value;
        if (RunProgram(ctx, "value-round-trip",
                std::string(kBox) +
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        Box<int> b = new Box<int>(2000000001);\n"
                "        int first = b.Get();\n"
                "        b.Set(first - 2000000000);\n"
                "        return first - 2000000000 + b.Get();\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Int);
            TEST_CHECK(ctx, value.intValue == 2);
        }
    }
    {
        // Two argument lists are two separate types: each has its own
        // field of its own type, and neither can be seen as the other.
        ScriptValue value;
        if (RunProgram(ctx, "arguments-make-separate-types",
                std::string(kBox) +
                "static class Program\n"
                "{\n"
                "    static float Main()\n"
                "    {\n"
                "        Box<int> counted = new Box<int>(3);\n"
                "        Box<float> measured = new Box<float>(2.5f);\n"
                "        Box<string> named = new Box<string>(\"ok\");\n"
                "        Box<bool> flagged = new Box<bool>(true);\n"
                "        if (named.Get() != \"ok\" || !flagged.Get()) { return 0.0f; }\n"
                "        return counted.Get() * 1.0f + measured.Get();\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Float);
            TEST_CHECK(ctx, value.floatValue == 5.5f);
        }
    }
    {
        // ... and the two really are separate declarations in the module,
        // laid out apart from each other. Only the one whose element is a
        // reference says so, which is what a collection reads.
        CompiledModule module;
        if (CompileProgram(ctx, "separate-layouts",
                std::string(kBox) +
                "class Node { int v; }\n"
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        Box<int> counted = new Box<int>(1);\n"
                "        Box<Node> held = new Box<Node>(new Node());\n"
                "        return counted.Get();\n"
                "    }\n"
                "}\n",
                module))
        {
            const ClassInfo* counted = FindClass(module, "Box<int>");
            const ClassInfo* held = FindClass(module, "Box<Node>");
            TEST_CHECK(ctx, counted != nullptr);
            TEST_CHECK(ctx, held != nullptr);
            if (counted && held)
            {
                TEST_CHECK(ctx, counted->fieldSlotCount == 1);
                TEST_CHECK(ctx, held->fieldSlotCount == 1);
                TEST_CHECK(ctx, !IsReferenceBitSet(counted->fieldReferenceBits, 0));
                TEST_CHECK(ctx, IsReferenceBitSet(held->fieldReferenceBits, 0));
            }

            // Each has its own bodies too, not one shared set.
            TEST_CHECK(ctx, FindFunction(module, "Box<int>.Get") != nullptr);
            TEST_CHECK(ctx, FindFunction(module, "Box<Node>.Get") != nullptr);

            // The pattern itself is not a type: nothing named after it
            // alone reaches the module.
            TEST_CHECK(ctx, FindClass(module, "Box") == nullptr);
            TEST_CHECK(ctx, FindFunction(module, "Box.Get") == nullptr);
        }
    }
    {
        // Nesting one argument list inside another: the inner type is
        // built first and then stands as the outer one's argument.
        ScriptValue value;
        if (RunProgram(ctx, "nested-arguments",
                std::string(kBox) +
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        Box<Box<int>> outer = new Box<Box<int>>(new Box<int>(21));\n"
                "        Box<int> inner = outer.Get();\n"
                "        return inner.Get() * 2;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 42);
        }
    }
    {
        // A method declared for dispatch in a built type is still
        // dispatched on in a class derived from it.
        ScriptValue value;
        if (RunProgram(ctx, "dispatch-through-a-built-base",
                "class Holder<T>\n"
                "{\n"
                "    T value;\n"
                "    Holder(T v) { this.value = v; }\n"
                "    virtual int Rank() { return 1; }\n"
                "    int Twice() { return this.Rank() * 2; }\n"
                "}\n"
                "class Special : Holder<int>\n"
                "{\n"
                "    Special(int v) : base(v) { }\n"
                "    override int Rank() { return 7; }\n"
                "}\n"
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        Holder<int> seen = new Special(3);\n"
                "        Holder<int> plain = new Holder<int>(3);\n"
                "        return seen.Rank() * 100 + seen.Twice() + plain.Rank();\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 715); // 7*100 + 14 + 1
        }
    }
    {
        // An interface written with a type parameter is a separate
        // interface per argument list, and a class implementing one of
        // them dispatches through it.
        ScriptValue value;
        if (RunProgram(ctx, "interface-with-arguments",
                "interface IProducer<T> { T Produce(); }\n"
                "class IntProducer : IProducer<int> { override int Produce() { return 7; } }\n"
                "class TextProducer : IProducer<string> { override string Produce() { return \"seven\"; } }\n"
                "static class Program\n"
                "{\n"
                "    static int Take(IProducer<int> source) { return source.Produce(); }\n"
                "    static int Main()\n"
                "    {\n"
                "        IProducer<int> numbers = new IntProducer();\n"
                "        IProducer<string> words = new TextProducer();\n"
                "        if (words.Produce() != \"seven\") { return 0; }\n"
                "        return Take(numbers) * 10 + numbers.Produce();\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 77);
        }
    }
    {
        // A pattern may be written in terms of its own parameter and
        // still build on another pattern.
        ScriptValue value;
        if (RunProgram(ctx, "pattern-built-on-a-pattern",
                std::string(kBox) +
                "class Labelled<T> : Box<T>\n"
                "{\n"
                "    string label;\n"
                "    Labelled(T v, string name) : base(v) { this.label = name; }\n"
                "    string Describe() { return this.label; }\n"
                "}\n"
                "static class Program\n"
                "{\n"
                "    static bool Main()\n"
                "    {\n"
                "        Labelled<int> counted = new Labelled<int>(5, \"count\");\n"
                "        Box<int> seen = counted;\n"
                "        return seen.Get() == 5 && counted.Describe() == \"count\";\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.boolValue);
        }
    }
    {
        // A parameter used as an element type produces a real sequence of
        // whatever it stood for.
        ScriptValue value;
        if (RunProgram(ctx, "parameter-as-an-element-type",
                "class Bag<T>\n"
                "{\n"
                "    T[] items;\n"
                "    Bag(int size) { this.items = new T[size]; }\n"
                "    void Put(int at, T v) { this.items[at] = v; }\n"
                "    T Take(int at) { return this.items[at]; }\n"
                "    int Size() { return this.items.Length; }\n"
                "}\n"
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        Bag<int> bag = new Bag<int>(4);\n"
                "        bag.Put(2, 9);\n"
                "        return bag.Take(2) * 10 + bag.Size();\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 94);
        }
    }
    {
        // A list whose elements are themselves lists: the prelude's
        // declaration is put through the same machinery as any other.
        ScriptValue value;
        if (RunProgram(ctx, "lists-of-lists",
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        List<List<int>> rows = new List<List<int>>();\n"
                "        List<int> first = new List<int>();\n"
                "        first.Add(4);\n"
                "        first.Add(5);\n"
                "        List<int> second = new List<int>();\n"
                "        second.Add(6);\n"
                "        rows.Add(first);\n"
                "        rows.Add(second);\n"
                "        return rows.Get(0).Get(1) * 100 + rows.Get(1).Get(0) * 10 + rows.Count();\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 562);
        }
    }
    {
        // A parameter stands for a reference type here, so comparing it
        // with nothing is allowed and means what it says.
        ScriptValue value;
        if (RunProgram(ctx, "parameter-compared-with-nothing",
                std::string(kBox) +
                "class Node { int v; }\n"
                "static class Program\n"
                "{\n"
                "    static bool Main()\n"
                "    {\n"
                "        Box<Node> empty = new Box<Node>(null);\n"
                "        Box<Node> filled = new Box<Node>(new Node());\n"
                "        return empty.Get() == null && filled.Get() != null;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.boolValue);
        }
    }

    ExpectRejected(ctx, "pattern-named-without-arguments",
        std::string(kBox) +
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        Box b;\n"
        "        return 0;\n"
        "    }\n"
        "}\n");

    ExpectRejected(ctx, "pattern-created-without-arguments",
        std::string(kBox) +
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        var b = new Box(1);\n"
        "        return 0;\n"
        "    }\n"
        "}\n");

    ExpectRejected(ctx, "wrong-number-of-arguments",
        std::string(kBox) +
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        Box<int, int> b = null;\n"
        "        return 0;\n"
        "    }\n"
        "}\n");

    ExpectRejected(ctx, "arguments-given-to-a-plain-type",
        "class Node { int v; }\n"
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        Node<int> n = null;\n"
        "        return 0;\n"
        "    }\n"
        "}\n");

    ExpectRejected(ctx, "two-built-types-are-not-interchangeable",
        std::string(kBox) +
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        Box<int> counted = new Box<int>(1);\n"
        "        Box<float> measured = counted;\n"
        "        return 0;\n"
        "    }\n"
        "}\n");

    // An operation a parameter does not support for the argument it was
    // given is refused where it is written, with the position of the
    // operator itself.
    ExpectRejected(ctx, "operator-the-argument-does-not-support",
        "class Node { int v; }\n"
        "class Adder<T>\n"
        "{\n"
        "    T Combine(T a, T b) { return a + b; }\n"
        "}\n"
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        Adder<Node> broken = new Adder<Node>();\n"
        "        return 0;\n"
        "    }\n"
        "}\n");

    ExpectRejected(ctx, "comparison-the-argument-does-not-support",
        "class Ranker<T>\n"
        "{\n"
        "    bool Before(T a, T b) { return a < b; }\n"
        "}\n"
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        Ranker<bool> broken = new Ranker<bool>();\n"
        "        return 0;\n"
        "    }\n"
        "}\n");

    ExpectRejected(ctx, "a-value-type-compared-with-nothing",
        std::string(kBox) +
        "class Checker<T>\n"
        "{\n"
        "    bool Empty(T v) { return v == null; }\n"
        "}\n"
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        Checker<int> broken = new Checker<int>();\n"
        "        return 0;\n"
        "    }\n"
        "}\n");

    ExpectRejected(ctx, "parameter-shadowing-a-declared-type",
        "class Node { int v; }\n"
        "class Wrapper<Node>\n"
        "{\n"
        "    Node value;\n"
        "}\n"
        "static class Program { static int Main() { return 0; } }\n");

    ExpectRejected(ctx, "a-pattern-that-builds-on-itself-without-end",
        std::string(kBox) +
        "class Endless<T>\n"
        "{\n"
        "    Endless<Box<T>> deeper;\n"
        "}\n"
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        Endless<int> start = null;\n"
        "        return 0;\n"
        "    }\n"
        "}\n");

    {
        // A type argument that only appears inside a method body is still
        // enough to have the declaration built, and the operator that
        // fails does so at the position where it is written.
        ScriptValue value;
        if (RunProgram(ctx, "argument-first-seen-inside-a-body",
                std::string(kBox) +
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        return new Box<int>(6).Get() * new Box<int>(7).Get();\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 42);
        }
    }
}
