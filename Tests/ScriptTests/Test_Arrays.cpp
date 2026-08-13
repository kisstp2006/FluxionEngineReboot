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
// producing a value. A fault is the whole point of these cases: reaching
// outside a sequence must be reported, never survived by writing over
// whatever happens to sit there.
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

const Diagnostic* FirstError(const DiagnosticList& diagnostics)
{
    for (const Diagnostic& entry : diagnostics.entries)
    {
        if (entry.severity == DiagnosticSeverity::Error) return &entry;
    }
    return nullptr;
}

// A rejection is only useful if it says where: every case below checks
// that the message arrived with a real line and column in the file that
// was compiled.
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

const char* const kNode =
    "class Node\n"
    "{\n"
    "    int v;\n"
    "    Node(int value) { this.v = value; }\n"
    "    int Read() { return this.v; }\n"
    "}\n";

} // namespace

void Test_Arrays_Run(TestContext& ctx)
{
    {
        // A computed index that lands exactly one past the end must fault
        // even though the constant-folding path never sees it.
        ExpectFault(ctx, "computed-index-past-end",
            "static class Program\n"
            "{\n"
            "    static int Main()\n"
            "    {\n"
            "        int[] a = new int[4];\n"
            "        int i = 0;\n"
            "        for (int k = 0; k < 5; k += 1) { i = k; }\n"
            "        return a[i];\n"
            "    }\n"
            "}\n",
            "Program.Main");
    }
    {
        // A negative computed index must fault, not wrap into the record
        // header or a neighbouring object.
        ExpectFault(ctx, "negative-computed-index",
            "static class Program\n"
            "{\n"
            "    static int Main()\n"
            "    {\n"
            "        int[] a = new int[4];\n"
            "        int i = 2;\n"
            "        i = i - 5;\n"
            "        return a[i];\n"
            "    }\n"
            "}\n",
            "Program.Main");
    }
    {
        // Writing through a null array reference is a fault, not a write
        // to address zero.
        ExpectFault(ctx, "store-through-null-array",
            "static class Program\n"
            "{\n"
            "    static int Main() { int[] a = null; a[0] = 1; return 0; }\n"
            "}\n",
            "Program.Main");
    }
    {
        // A zero-length array is legal; its Length is 0 and any index
        // into it faults.
        ScriptValue value;
        if (RunProgram(ctx, "zero-length-array",
                "static class Program { static int Main() { int[] a = new int[0]; return a.Length; } }\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 0);
        }
        ExpectFault(ctx, "index-into-zero-length",
            "static class Program { static int Main() { int[] a = new int[0]; return a[0]; } }\n",
            "Program.Main");
    }
    {
        // Each row of an array-of-arrays is its own object with its own
        // length, and the rows do not alias.
        ScriptValue value;
        if (RunProgram(ctx, "jagged-rows-are-independent",
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        int[][] rows = new int[2][];\n"
                "        rows[0] = new int[2];\n"
                "        rows[1] = new int[3];\n"
                "        rows[0][0] = 7;\n"
                "        rows[1][0] = 9;\n"
                "        return rows[0][0] * 100 + rows[1][0] * 10 + rows[1].Length;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 793);
        }
    }
    {
        // A sequence of a value type starts at that type's own zero, with
        // no initialization written anywhere.
        ScriptValue value;
        if (RunProgram(ctx, "value-element-defaults",
                "static class Program\n"
                "{\n"
                "    static bool Main()\n"
                "    {\n"
                "        int[] numbers = new int[3];\n"
                "        float[] weights = new float[2];\n"
                "        bool[] flags = new bool[2];\n"
                "        string[] names = new string[2];\n"
                "        return numbers[0] == 0 && numbers[2] == 0 && weights[1] == 0.0f && !flags[0] && names[1] == \"\";\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Bool);
            TEST_CHECK(ctx, value.boolValue);
        }
    }
    {
        // A sequence of a reference type starts out holding nothing at
        // all, which is what a reference field does too.
        ScriptValue value;
        if (RunProgram(ctx, "reference-element-defaults",
                std::string(kNode) +
                "static class Program\n"
                "{\n"
                "    static bool Main()\n"
                "    {\n"
                "        Node[] nodes = new Node[4];\n"
                "        return nodes[0] == null && nodes[3] == null;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.boolValue);
        }
    }
    {
        // Written, then read back: each position holds what was put there
        // and nothing else moved.
        ScriptValue value;
        if (RunProgram(ctx, "element-round-trip",
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        int[] numbers = new int[8];\n"
                "        numbers[0] = 5;\n"
                "        numbers[7] = 9;\n"
                "        numbers[3] = numbers[0] + numbers[7];\n"
                "        numbers[3] += 1;\n"
                "        return numbers[0] * 1000 + numbers[3] * 10 + numbers[7];\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Int);
            TEST_CHECK(ctx, value.intValue == 5159); // 5*1000 + 15*10 + 9
        }
    }
    {
        // The length a sequence was created with is readable from it, and
        // is not the same thing as how much of it has been written.
        ScriptValue value;
        if (RunProgram(ctx, "length-is-what-it-was-created-with",
                std::string(kNode) +
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        int[] numbers = new int[8];\n"
                "        Node[] nodes = new Node[4];\n"
                "        int[] empty = new int[0];\n"
                "        return numbers.Length * 100 + nodes.Length * 10 + empty.Length;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 840);
        }
    }
    {
        // A sequence holding sequences: the outer one starts out holding
        // nothing, and each inner one is created on its own.
        ScriptValue value;
        if (RunProgram(ctx, "sequence-of-sequences",
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        int[][] grid = new int[3][];\n"
                "        if (grid[0] != null) { return -1; }\n"
                "        grid[0] = new int[2];\n"
                "        grid[1] = new int[4];\n"
                "        grid[0][1] = 7;\n"
                "        grid[1][3] = 5;\n"
                "        return grid[0][1] * 100 + grid[1][3] * 10 + grid[1].Length;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 754);
        }
    }
    {
        // A sequence whose elements are a type built from a type argument
        // holds real instances of it.
        ScriptValue value;
        if (RunProgram(ctx, "sequence-of-built-types",
                "class Box<T>\n"
                "{\n"
                "    T value;\n"
                "    Box(T v) { this.value = v; }\n"
                "    T Get() { return this.value; }\n"
                "}\n"
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        Box<int>[] boxes = new Box<int>[3];\n"
                "        if (boxes[2] != null) { return -1; }\n"
                "        boxes[0] = new Box<int>(11);\n"
                "        boxes[2] = new Box<int>(31);\n"
                "        return boxes[0].Get() + boxes[2].Get() + boxes.Length;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 45); // 11 + 31 + 3
        }
    }

    // Reaching outside the range a sequence was created with is reported,
    // from either side and from either direction.
    ExpectFault(ctx, "read-past-the-end",
        "static class Program\n"
        "{\n"
        "    static int Main() { int[] numbers = new int[4]; return numbers[4]; }\n"
        "}\n",
        "Program.Main");

    ExpectFault(ctx, "read-before-the-start",
        "static class Program\n"
        "{\n"
        "    static int Main() { int[] numbers = new int[4]; int at = 0 - 1; return numbers[at]; }\n"
        "}\n",
        "Program.Main");

    ExpectFault(ctx, "write-past-the-end",
        "static class Program\n"
        "{\n"
        "    static int Main() { int[] numbers = new int[4]; numbers[4] = 1; return 0; }\n"
        "}\n",
        "Program.Main");

    ExpectFault(ctx, "write-before-the-start",
        "static class Program\n"
        "{\n"
        "    static int Main() { int[] numbers = new int[4]; int at = 0 - 3; numbers[at] = 1; return 0; }\n"
        "}\n",
        "Program.Main");

    ExpectFault(ctx, "read-outside-an-empty-sequence",
        "static class Program\n"
        "{\n"
        "    static int Main() { int[] none = new int[0]; return none[0]; }\n"
        "}\n",
        "Program.Main");

    ExpectFault(ctx, "negative-length",
        "static class Program\n"
        "{\n"
        "    static int Main() { int[] numbers = new int[-1]; return numbers.Length; }\n"
        "}\n",
        "Program.Main");

    ExpectFault(ctx, "computed-negative-length",
        "static class Program\n"
        "{\n"
        "    static int Main() { int n = 2; int[] numbers = new int[n - 5]; return numbers.Length; }\n"
        "}\n",
        "Program.Main");

    ExpectFault(ctx, "length-of-nothing",
        "static class Program\n"
        "{\n"
        "    static int Main() { int[] numbers = null; return numbers.Length; }\n"
        "}\n",
        "Program.Main");

    ExpectFault(ctx, "element-of-nothing",
        "static class Program\n"
        "{\n"
        "    static int Main() { int[] numbers = null; return numbers[0]; }\n"
        "}\n",
        "Program.Main");

    // A sequence of one type is not a sequence of another, however the two
    // element types are related: writing a Derived into a Base[] that is
    // really a Derived[] would be a hole nothing could check for.
    ExpectRejected(ctx, "sequence-of-derived-is-not-a-sequence-of-base",
        "class Base { int a; }\n"
        "class Derived : Base { int b; }\n"
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        Derived[] derived = new Derived[2];\n"
        "        Base[] seen = derived;\n"
        "        return seen.Length;\n"
        "    }\n"
        "}\n");

    ExpectRejected(ctx, "position-that-is-not-a-number",
        "static class Program\n"
        "{\n"
        "    static int Main() { int[] numbers = new int[2]; return numbers[\"one\"]; }\n"
        "}\n");

    ExpectRejected(ctx, "indexing-something-with-no-elements",
        "static class Program\n"
        "{\n"
        "    static int Main() { int plain = 3; return plain[0]; }\n"
        "}\n");

    ExpectRejected(ctx, "assigning-to-the-length",
        "static class Program\n"
        "{\n"
        "    static int Main() { int[] numbers = new int[2]; numbers.Length = 5; return 0; }\n"
        "}\n");

    // --- Walking a sequence ------------------------------------------------

    {
        ScriptValue value;
        if (RunProgram(ctx, "foreach-over-a-sequence",
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        int[] numbers = new int[4];\n"
                "        numbers[0] = 1;\n"
                "        numbers[1] = 2;\n"
                "        numbers[2] = 3;\n"
                "        numbers[3] = 4;\n"
                "        int total = 0;\n"
                "        foreach (int x in numbers) { total += x; }\n"
                "        return total;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 10);
        }
    }
    {
        // The two ways out of a loop mean the same thing here as in every
        // other loop.
        ScriptValue value;
        if (RunProgram(ctx, "foreach-break-and-continue",
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        int[] numbers = new int[6];\n"
                "        for (int i = 0; i < 6; i += 1) { numbers[i] = i; }\n"
                "\n"
                "        int skipped = 0;\n"
                "        foreach (int x in numbers)\n"
                "        {\n"
                "            if (x % 2 == 0) { continue; }\n"
                "            skipped += x;\n"
                "        }\n"
                "\n"
                "        int stopped = 0;\n"
                "        foreach (int x in numbers)\n"
                "        {\n"
                "            if (x == 3) { break; }\n"
                "            stopped += x;\n"
                "        }\n"
                "\n"
                "        return skipped * 100 + stopped;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 903); // (1+3+5)*100 + (0+1+2)
        }
    }
    {
        // Walking a sequence of references reaches the objects themselves,
        // not copies of them.
        ScriptValue value;
        if (RunProgram(ctx, "foreach-over-references",
                std::string(kNode) +
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        Node[] nodes = new Node[3];\n"
                "        nodes[0] = new Node(4);\n"
                "        nodes[1] = new Node(5);\n"
                "        nodes[2] = new Node(6);\n"
                "        int total = 0;\n"
                "        foreach (Node n in nodes) { total += n.Read(); }\n"
                "        return total;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 15);
        }
    }
    {
        // A nested walk keeps each loop's own position, and the inner one
        // starts over every time round the outer.
        ScriptValue value;
        if (RunProgram(ctx, "nested-foreach",
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        int[] outer = new int[3];\n"
                "        outer[0] = 1; outer[1] = 2; outer[2] = 3;\n"
                "        int[] inner = new int[2];\n"
                "        inner[0] = 10; inner[1] = 20;\n"
                "        int total = 0;\n"
                "        foreach (int a in outer)\n"
                "        {\n"
                "            foreach (int b in inner) { total += a * b; }\n"
                "        }\n"
                "        return total;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 180); // (1+2+3) * (10+20)
        }
    }

    ExpectFault(ctx, "foreach-over-nothing",
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        int[] numbers = null;\n"
        "        int total = 0;\n"
        "        foreach (int x in numbers) { total += x; }\n"
        "        return total;\n"
        "    }\n"
        "}\n",
        "Program.Main");

    ExpectRejected(ctx, "assigning-to-the-loop-variable",
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        int[] numbers = new int[2];\n"
        "        foreach (int x in numbers) { x = 5; }\n"
        "        return 0;\n"
        "    }\n"
        "}\n");

    ExpectRejected(ctx, "adding-to-the-loop-variable",
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        int[] numbers = new int[2];\n"
        "        foreach (int x in numbers) { x += 5; }\n"
        "        return 0;\n"
        "    }\n"
        "}\n");

    ExpectRejected(ctx, "walking-something-with-no-elements",
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        int plain = 3;\n"
        "        foreach (int x in plain) { }\n"
        "        return 0;\n"
        "    }\n"
        "}\n");

    ExpectRejected(ctx, "loop-variable-of-the-wrong-type",
        std::string(kNode) +
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        int[] numbers = new int[2];\n"
        "        foreach (Node n in numbers) { }\n"
        "        return 0;\n"
        "    }\n"
        "}\n");

    // --- The list the prelude declares -------------------------------------

    {
        // Adding well past what the list started with proves the copy the
        // growth path performs: every element still reads back as what was
        // put in, at the position it was put in at.
        ScriptValue value;
        if (RunProgram(ctx, "list-grows-and-keeps-everything",
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        List<int> values = new List<int>();\n"
                "        for (int i = 0; i < 100; i += 1) { values.Add(i * 3); }\n"
                "        if (values.Count() != 100) { return -1; }\n"
                "        for (int i = 0; i < 100; i += 1)\n"
                "        {\n"
                "            if (values.Get(i) != i * 3) { return -2 - i; }\n"
                "        }\n"
                "        return values.Count();\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 100);
        }
    }
    {
        // Removing shifts everything after the hole down by one, and the
        // count follows.
        ScriptValue value;
        if (RunProgram(ctx, "list-remove-shifts",
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        List<int> values = new List<int>();\n"
                "        for (int i = 0; i < 6; i += 1) { values.Add(i); }\n"
                "        values.RemoveAt(2);\n"
                "        values.RemoveAt(0);\n"
                "        int packed = 0;\n"
                "        for (int i = 0; i < values.Count(); i += 1) { packed = packed * 10 + values.Get(i); }\n"
                "        return packed * 10 + values.Count();\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 13454); // 1,3,4,5 packed, then the count
        }
    }
    {
        ScriptValue value;
        if (RunProgram(ctx, "list-clear-and-set",
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        List<int> values = new List<int>();\n"
                "        for (int i = 0; i < 20; i += 1) { values.Add(i); }\n"
                "        values.Set(3, 99);\n"
                "        int set = values.Get(3);\n"
                "        values.Clear();\n"
                "        if (values.Count() != 0) { return -1; }\n"
                "        values.Add(7);\n"
                "        return set * 100 + values.Get(0) * 10 + values.Count();\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 9971);
        }
    }
    {
        ScriptValue value;
        if (RunProgram(ctx, "foreach-over-a-list",
                std::string(kNode) +
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        List<Node> nodes = new List<Node>();\n"
                "        for (int i = 1; i <= 5; i += 1) { nodes.Add(new Node(i)); }\n"
                "        int total = 0;\n"
                "        foreach (Node n in nodes)\n"
                "        {\n"
                "            if (n.Read() == 4) { continue; }\n"
                "            if (n.Read() == 5) { break; }\n"
                "            total += n.Read();\n"
                "        }\n"
                "        return total;\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 6); // 1 + 2 + 3
        }
    }
    {
        // A list of lists: the element type is itself built from a type
        // argument, and each inner list is its own object.
        ScriptValue value;
        if (RunProgram(ctx, "list-of-lists",
                "static class Program\n"
                "{\n"
                "    static int Main()\n"
                "    {\n"
                "        List<List<int>> rows = new List<List<int>>();\n"
                "        for (int r = 0; r < 3; r += 1)\n"
                "        {\n"
                "            List<int> row = new List<int>();\n"
                "            for (int c = 0; c < 4; c += 1) { row.Add(r * 10 + c); }\n"
                "            rows.Add(row);\n"
                "        }\n"
                "        int total = 0;\n"
                "        foreach (List<int> row in rows)\n"
                "        {\n"
                "            foreach (int cell in row) { total += cell; }\n"
                "        }\n"
                "        return total * 10 + rows.Count();\n"
                "    }\n"
                "}\n",
                "Program.Main", value))
        {
            // (0+1+2+3) + (10+11+12+13) + (20+21+22+23) = 6 + 46 + 86 = 138
            TEST_CHECK(ctx, value.intValue == 1383);
        }
    }

    ExpectFault(ctx, "list-read-past-what-was-added",
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        List<int> values = new List<int>();\n"
        "        values.Add(1);\n"
        "        return values.Get(1);\n"
        "    }\n"
        "}\n",
        "Program.Main");

    ExpectFault(ctx, "list-read-before-the-start",
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        List<int> values = new List<int>();\n"
        "        values.Add(1);\n"
        "        return values.Get(0 - 1);\n"
        "    }\n"
        "}\n",
        "Program.Main");

    ExpectFault(ctx, "list-remove-outside-what-was-added",
        "static class Program\n"
        "{\n"
        "    static int Main()\n"
        "    {\n"
        "        List<int> values = new List<int>();\n"
        "        values.RemoveAt(0);\n"
        "        return 0;\n"
        "    }\n"
        "}\n",
        "Program.Main");
}
