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

// Keeps the machine alive after the call returns, which is what the
// assertions here need: the heap has to be looked at from outside once
// the script has stopped running.
class ScriptRun
{
public:
    ScriptRun(TestContext& ctx, const char* label, const std::string& source)
        : m_ctx(ctx), m_label(label)
    {
        DiagnosticList diagnostics;
        CompileOptions options;
        options.fileName = label;

        auto compiled = Compile(source, options, diagnostics);
        if (!compiled.IsOk())
        {
            std::fprintf(stderr, "  FAIL: '%s' did not compile\n", label);
            ReportDiagnostics(diagnostics);
            m_ctx.failures++;
            return;
        }

        m_vm = CreateVm(compiled.Value(), diagnostics);
        if (!m_vm)
        {
            std::fprintf(stderr, "  FAIL: '%s' did not load\n", label);
            ReportDiagnostics(diagnostics);
            m_ctx.failures++;
        }
    }

    ~ScriptRun() { DestroyVm(m_vm); }

    ScriptRun(const ScriptRun&) = delete;
    ScriptRun& operator=(const ScriptRun&) = delete;

    bool Ready() const { return m_vm != nullptr; }
    Vm* Machine() const { return m_vm; }

    bool Call(const char* entry, ScriptValue& outValue)
    {
        if (!m_vm) return false;

        auto result = Invoke(m_vm, entry);
        if (!result.IsOk())
        {
            std::fprintf(stderr, "  FAIL: '%s' faulted running %s: %s\n", m_label, entry,
                result.Status().message ? result.Status().message : "<no message>");
            m_ctx.failures++;
            return false;
        }
        outValue = result.Value();
        return true;
    }

private:
    TestContext& m_ctx;
    const char* m_label;
    Vm* m_vm = nullptr;
};

// One small class with a reference field, which is all it takes to build
// both a chain and a cycle, plus a helper that produces nothing but
// garbage.
const char* const kNode =
    "class Node\n"
    "{\n"
    "    float v;\n"
    "    Node next;\n"
    "\n"
    "    Node(float value) { this.v = value; }\n"
    "\n"
    "    float Read() { return this.v; }\n"
    "}\n"
    "\n"
    "static class Junk\n"
    "{\n"
    "    static int Churn(int n)\n"
    "    {\n"
    "        for (int i = 0; i < n; i += 1)\n"
    "        {\n"
    "            Node t = new Node(1.0f);\n"
    "        }\n"
    "        return n;\n"
    "    }\n"
    "}\n";

} // namespace

void Test_Gc_Run(TestContext& ctx)
{
    {
        // Garbage produced underneath a call that sits in the middle of an
        // enclosing expression still has to be reclaimed eventually. If
        // collection could only ever happen with every caller between
        // statements, this shape would keep the heap growing.
        ScriptRun run(ctx, "garbage-under-mid-expression-call",
            std::string(kNode) +
            "static class Program\n"
            "{\n"
            "    static int Step(int n) { Junk.Churn(200); Gc.Collect(); return n; }\n"
            "    static int Main()\n"
            "    {\n"
            "        int total = 0;\n"
            "        for (int i = 0; i < 20; i += 1) { total = 1 + Step(i) + 1; }\n"
            "        Gc.Collect();\n"
            "        return Gc.LiveObjects();\n"
            "    }\n"
            "}\n");
        ScriptValue value;
        if (run.Ready() && run.Call("Program.Main", value))
        {
            // 20 iterations x 200 objects = 4000 allocations, all garbage.
            TEST_CHECK(ctx, value.intValue < 200);
            const HeapStats stats = GetHeapStats(run.Machine());
            TEST_CHECK(ctx, stats.totalAllocations >= 4000);
            TEST_CHECK(ctx, stats.liveObjects < 200);
        }
    }
    {
        // Marking has to follow reference fields transitively: only the
        // head of the chain is held by a local, but every link must
        // survive and still read back correctly.
        ScriptRun run(ctx, "transitive-marking",
            std::string(kNode) +
            "static class Program\n"
            "{\n"
            "    static float Main()\n"
            "    {\n"
            "        Node head = new Node(1.0f);\n"
            "        head.next = new Node(2.0f);\n"
            "        head.next.next = new Node(3.0f);\n"
            "        Junk.Churn(300);\n"
            "        Gc.Collect();\n"
            "        return head.Read() + head.next.Read() + head.next.next.Read();\n"
            "    }\n"
            "}\n");
        ScriptValue value;
        if (run.Ready() && run.Call("Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Float);
            TEST_CHECK(ctx, value.floatValue == 6.0f);
        }
    }
    {
        // An object reachable only through a chain that a *caller* frame
        // holds must survive a collection forced deeper in the call stack.
        ScriptRun run(ctx, "caller-frame-chain-survives",
            std::string(kNode) +
            "static class Program\n"
            "{\n"
            "    static float Deep(int depth) { if (depth > 0) { return Deep(depth - 1); } Junk.Churn(300); Gc.Collect(); return 0.0f; }\n"
            "    static float Main()\n"
            "    {\n"
            "        Node head = new Node(4.0f);\n"
            "        head.next = new Node(5.0f);\n"
            "        float ignored = Deep(6);\n"
            "        Gc.Collect();\n"
            "        return head.Read() + head.next.Read() + ignored;\n"
            "    }\n"
            "}\n");
        ScriptValue value;
        if (run.Ready() && run.Call("Program.Main", value))
        {
            TEST_CHECK(ctx, value.floatValue == 9.0f);
        }
    }
    {
        // Objects nothing holds on to are reclaimed: what stays live is a
        // tiny fraction of what was created.
        ScriptRun run(ctx, "short-lived-objects",
            std::string(kNode) +
            "static class Program\n"
            "{\n"
            "    static int Main() { return Junk.Churn(2000); }\n"
            "}\n");

        ScriptValue value;
        if (run.Ready() && run.Call("Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 2000);

            const HeapStats duringRun = GetHeapStats(run.Machine());
            TEST_CHECK(ctx, duringRun.totalAllocations == 2000);
            TEST_CHECK(ctx, duringRun.collectionCount > 0);
            TEST_CHECK(ctx, duringRun.liveObjects < 200);

            CollectGarbage(run.Machine());
            const HeapStats afterRun = GetHeapStats(run.Machine());
            TEST_CHECK(ctx, afterRun.liveObjects == 0);
            TEST_CHECK(ctx, afterRun.totalAllocations == 2000);
        }
    }
    {
        // An object a local still names survives, and is still the object
        // it was: the value read out of it afterwards is the one that was
        // put in.
        ScriptRun run(ctx, "live-local-survives",
            std::string(kNode) +
            "static class Program\n"
            "{\n"
            "    static float Main()\n"
            "    {\n"
            "        Node keep = new Node(4.5f);\n"
            "        Junk.Churn(500);\n"
            "        Gc.Collect();\n"
            "        float v = keep.Read();\n"
            "        return v + keep.v;\n"
            "    }\n"
            "}\n");

        ScriptValue value;
        if (run.Ready() && run.Call("Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Float);
            TEST_CHECK(ctx, value.floatValue == 9.0f);
            TEST_CHECK(ctx, GetHeapStats(run.Machine()).collectionCount > 0);
        }
    }
    {
        // Two objects that reference only each other are still garbage
        // once nothing else names them. Counting references would keep
        // them forever; tracing from the roots does not.
        ScriptRun run(ctx, "cycle-is-reclaimed",
            std::string(kNode) +
            "static class Program\n"
            "{\n"
            "    static void BuildCycle()\n"
            "    {\n"
            "        Node a = new Node(1.0f);\n"
            "        Node b = new Node(2.0f);\n"
            "        a.next = b;\n"
            "        b.next = a;\n"
            "    }\n"
            "\n"
            "    static int Main()\n"
            "    {\n"
            "        Node keep = new Node(9.0f);\n"
            "        BuildCycle();\n"
            "        Gc.Collect();\n"
            "        int live = Gc.LiveObjects();\n"
            "        return live;\n"
            "    }\n"
            "}\n");

        ScriptValue value;
        if (run.Ready() && run.Call("Program.Main", value))
        {
            // Only the object still named by a local is left, so both
            // halves of the cycle went away.
            TEST_CHECK(ctx, value.intValue == 1);
            TEST_CHECK(ctx, GetHeapStats(run.Machine()).totalAllocations == 3);
        }
    }
    {
        // A chain reachable from one local keeps its whole length alive,
        // which is the transitive half of the same story.
        ScriptRun run(ctx, "reachable-chain-survives",
            std::string(kNode) +
            "static class Program\n"
            "{\n"
            "    static int Main()\n"
            "    {\n"
            "        Node head = new Node(1.0f);\n"
            "        head.next = new Node(2.0f);\n"
            "        head.next.next = new Node(3.0f);\n"
            "        Junk.Churn(400);\n"
            "        Gc.Collect();\n"
            "        int live = Gc.LiveObjects();\n"
            "        float tail = head.next.next.v;\n"
            "        if (tail != 3.0f) { return 0; }\n"
            "        return live;\n"
            "    }\n"
            "}\n");

        ScriptValue value;
        if (run.Ready() && run.Call("Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 3);
        }
    }
    {
        // A collection asked for deep in a call chain still sees the
        // locals of every frame below it, so each level's own object is
        // readable once control comes back.
        ScriptRun run(ctx, "deeper-frame-locals-survive",
            std::string(kNode) +
            "static class Program\n"
            "{\n"
            "    static float Deep(int depth)\n"
            "    {\n"
            "        Node mine = new Node(depth * 1.0f + 1.0f);\n"
            "        if (depth > 0)\n"
            "        {\n"
            "            float inner = Deep(depth - 1);\n"
            "            return inner + mine.v;\n"
            "        }\n"
            "        Gc.Collect();\n"
            "        Junk.Churn(300);\n"
            "        float own = mine.v;\n"
            "        return own;\n"
            "    }\n"
            "\n"
            "    static float Main() { return Deep(3); }\n"
            "}\n");

        ScriptValue value;
        if (run.Ready() && run.Call("Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Float);
            TEST_CHECK(ctx, value.floatValue == 10.0f); // 4 + 3 + 2 + 1
            TEST_CHECK(ctx, GetHeapStats(run.Machine()).collectionCount > 0);
        }
    }
    {
        // A loop that allocates for a long time settles at a live count
        // that has nothing to do with how long it ran.
        ScriptRun run(ctx, "long-loop-stays-bounded",
            std::string(kNode) +
            "static class Program\n"
            "{\n"
            "    static int Main()\n"
            "    {\n"
            "        for (int i = 0; i < 20000; i += 1)\n"
            "        {\n"
            "            Node t = new Node(1.0f);\n"
            "        }\n"
            "        return Gc.LiveObjects();\n"
            "    }\n"
            "}\n");

        ScriptValue value;
        if (run.Ready() && run.Call("Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue > 0);
            TEST_CHECK(ctx, value.intValue < 200);

            const HeapStats stats = GetHeapStats(run.Machine());
            TEST_CHECK(ctx, stats.totalAllocations == 20000);
            TEST_CHECK(ctx, stats.collectionCount > 50);
            TEST_CHECK(ctx, stats.liveObjects < 200);
        }
    }
    {
        // A record is reused once its occupant is gone, and the reference
        // that named the old occupant knows it: the generation, not the
        // index, is what makes two handles the same object.
        ScriptRun run(ctx, "stale-handles-are-recognized",
            std::string(kNode) +
            "static class Program\n"
            "{\n"
            "    static Node Make() { return new Node(7.0f); }\n"
            "}\n");

        ScriptValue first;
        ScriptValue second;
        if (run.Ready() && run.Call("Program.Make", first))
        {
            TEST_CHECK(ctx, first.type == ValueType::Object);
            TEST_CHECK(ctx, !first.objectValue.IsNull());
            TEST_CHECK(ctx, IsObjectAlive(run.Machine(), first.objectValue));

            CollectGarbage(run.Machine());
            TEST_CHECK(ctx, !IsObjectAlive(run.Machine(), first.objectValue));

            if (run.Call("Program.Make", second))
            {
                TEST_CHECK(ctx, IsObjectAlive(run.Machine(), second.objectValue));
                TEST_CHECK(ctx, !IsObjectAlive(run.Machine(), first.objectValue));
                TEST_CHECK(ctx, first.objectValue != second.objectValue);
                TEST_CHECK(ctx, GetHeapStats(run.Machine()).totalAllocations == 2);
            }
        }
    }
    {
        // What makes the collection precise rather than a guess: a frame
        // slot freed by one scope is never handed to a declaration of the
        // other kind, so the bitmap describing the frame is true for the
        // whole call and not only for whichever declaration came last.
        DiagnosticList diagnostics;
        CompileOptions options;
        options.fileName = "slot-kinds-stay-apart";

        auto compiled = Compile(
            std::string(kNode) +
            "static class Program\n"
            "{\n"
            "    static int Main()\n"
            "    {\n"
            "        { Node held = new Node(1.0f); }\n"
            "        { int plain = 7; }\n"
            "        return 0;\n"
            "    }\n"
            "}\n",
            options, diagnostics);
        TEST_CHECK(ctx, compiled.IsOk());
        if (compiled.IsOk())
        {
            const FunctionInfo* main = nullptr;
            for (const FunctionInfo& function : compiled.Value().functions)
            {
                if (function.qualifiedName == "Program.Main") main = &function;
            }
            TEST_CHECK(ctx, main != nullptr);
            if (main)
            {
                // Two sibling scopes that would otherwise have shared one
                // slot, kept apart because one holds a reference.
                TEST_CHECK(ctx, main->localSlotCount == 2);
                TEST_CHECK(ctx, IsReferenceBitSet(main->localReferenceBits, 0));
                TEST_CHECK(ctx, !IsReferenceBitSet(main->localReferenceBits, 1));
            }
        }
    }
    {
        // Native code holding an object says so, and that alone keeps it
        // and everything it reaches alive across a collection.
        ScriptRun run(ctx, "pinned-roots-survive",
            std::string(kNode) +
            "static class Program\n"
            "{\n"
            "    static Node MakeChain()\n"
            "    {\n"
            "        Node head = new Node(1.0f);\n"
            "        head.next = new Node(2.0f);\n"
            "        return head;\n"
            "    }\n"
            "}\n");

        ScriptValue head;
        if (run.Ready() && run.Call("Program.MakeChain", head))
        {
            TEST_CHECK(ctx, PinObject(run.Machine(), head.objectValue));

            CollectGarbage(run.Machine());
            TEST_CHECK(ctx, IsObjectAlive(run.Machine(), head.objectValue));

            // The pin reaches the whole chain, not only the object named.
            TEST_CHECK(ctx, GetHeapStats(run.Machine()).liveObjects == 2);

            UnpinObject(run.Machine(), head.objectValue);
            CollectGarbage(run.Machine());
            TEST_CHECK(ctx, !IsObjectAlive(run.Machine(), head.objectValue));
            TEST_CHECK(ctx, GetHeapStats(run.Machine()).liveObjects == 0);
        }
    }
}
