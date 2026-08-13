#include "TestFramework.h"

#include <Fluxion/Core/Reflection/Reflection.hpp>
#include <Fluxion/Core/Service/Service.hpp>
#include <Fluxion/Core/Service/ServiceRegistry.h>
#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Script/Script.hpp>
#include <Fluxion/Script/ScriptRuntimeService.hpp>

#include <cstdio>
#include <string>
#include <vector>

using namespace Fluxion::Script;

namespace
{

void ReportDiagnostics(const DiagnosticList& diagnostics)
{
    for (const Diagnostic& entry : diagnostics.entries)
        std::fprintf(stderr, "  %s:%u:%u: %s\n", entry.location.file.c_str(), entry.location.line, entry.location.column, entry.message.c_str());
}

// --- A stand-in for the engine side ------------------------------------

// The object the engine owns. Scripts never hold one of these: they hold
// a handle, and the store below is the only thing that can turn a handle
// back into the object.
struct TestNode
{
    static constexpr auto Name = "TestNode";

    i32 value = 0;

    i32 Add(i32 a, i32 b)
    {
        value += a + b;
        return value;
    }

    f32 Blend(f32 weight, i32 count) const { return weight * (f32)count; }

    i32 Read() const { return value; }

    void Set(i32 next) { value = next; }
};

// Indices start at one and generations at one, so a handle looks exactly
// like a live script reference does -- which is the whole point of the
// assertions below about the collector.
class TestStore
{
public:
    TestStore() { m_records.emplace_back(); }

    EngineHandle Spawn(i32 value)
    {
        Record created;
        created.node.value = value;
        created.generation = 1;
        created.live = true;

        const u32 index = (u32)m_records.size();
        m_records.push_back(created);
        return EngineHandle{ index, created.generation };
    }

    TestNode* Resolve(EngineHandle handle)
    {
        if (handle.index == 0 || handle.index >= m_records.size()) return nullptr;
        Record& record = m_records[handle.index];
        if (!record.live || record.generation != handle.generation) return nullptr;
        return &record.node;
    }

    bool IsAlive(EngineHandle handle) { return Resolve(handle) != nullptr; }

    void Retire(EngineHandle handle)
    {
        if (handle.index == 0 || handle.index >= m_records.size()) return;
        Record& record = m_records[handle.index];
        if (record.generation != handle.generation) return;
        record.live = false;
        ++record.generation;
    }

private:
    struct Record
    {
        TestNode node;
        u32 generation = 0;
        bool live = false;
    };

    std::vector<Record> m_records;
};

TestStore& Store()
{
    static TestStore store;
    return store;
}

// The static side of the engine's surface: free functions, which is what
// the reflection generator reads as needing no instance.
EngineHandle TestWorld_Spawn(i32 value) { return Store().Spawn(value); }
EngineHandle TestWorld_Echo(EngineHandle handle) { return handle; }
i32 TestWorld_ValueOf(EngineHandle handle)
{
    TestNode* node = Store().Resolve(handle);
    return node ? node->value : -1;
}
bool TestWorld_Same(EngineHandle a, EngineHandle b) { return a == b; }

void* ResolveTestNode(void* user, EngineHandle handle)
{
    (void)user;
    return Store().Resolve(handle);
}

// Owns everything the descriptors point at, since neither the reflection
// registry nor the binding table copies any of it.
class EngineSurface
{
public:
    EngineSurface()
    {
        m_addParameters[0] = FLUXION_TYPE_ID_OF(i32);
        m_addParameters[1] = FLUXION_TYPE_ID_OF(i32);
        m_blendParameters[0] = FLUXION_TYPE_ID_OF(f32);
        m_blendParameters[1] = FLUXION_TYPE_ID_OF(i32);
        m_setParameters[0] = FLUXION_TYPE_ID_OF(i32);

        m_spawnParameters[0] = FLUXION_TYPE_ID_OF(i32);
        m_handleParameters[0] = FLUXION_TYPE_ID_OF(TestNode);
        m_twoHandleParameters[0] = FLUXION_TYPE_ID_OF(TestNode);
        m_twoHandleParameters[1] = FLUXION_TYPE_ID_OF(TestNode);

        m_nodeMethods.push_back(Fluxion::Core::ReflectMethod<&TestNode::Add>("Add", FLUXION_TYPE_ID_OF(i32), m_addParameters));
        m_nodeMethods.push_back(Fluxion::Core::ReflectMethod<&TestNode::Blend>("Blend", FLUXION_TYPE_ID_OF(f32), m_blendParameters));
        m_nodeMethods.push_back(Fluxion::Core::ReflectMethod<&TestNode::Read>("Read", FLUXION_TYPE_ID_OF(i32)));
        m_nodeMethods.push_back(Fluxion::Core::ReflectMethod<&TestNode::Set>("Set", FLUXION_TYPE_ID_INVALID, m_setParameters));

        m_worldMethods.push_back(Fluxion::Core::ReflectMethod<&TestWorld_Spawn>("Spawn", FLUXION_TYPE_ID_OF(TestNode), m_spawnParameters));
        m_worldMethods.push_back(Fluxion::Core::ReflectMethod<&TestWorld_Echo>("Echo", FLUXION_TYPE_ID_OF(TestNode), m_handleParameters));
        m_worldMethods.push_back(Fluxion::Core::ReflectMethod<&TestWorld_ValueOf>("ValueOf", FLUXION_TYPE_ID_OF(i32), m_handleParameters));
        m_worldMethods.push_back(Fluxion::Core::ReflectMethod<&TestWorld_Same>("Same", FLUXION_TYPE_ID_OF(bool), m_twoHandleParameters));

        m_nodeType.name = Fluxion_StringView_FromCStr("TestNode");
        m_nodeType.id = FLUXION_TYPE_ID_OF(TestNode);
        m_nodeType.kind = FLUXION_TYPE_KIND_STRUCT;
        m_nodeType.size = sizeof(TestNode);
        m_nodeType.version = 1;
        m_nodeType.members = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionPropertyInfo));
        m_nodeType.methods = Fluxion_Span_Make(m_nodeMethods.data(), m_nodeMethods.size(), sizeof(FluxionMethodInfo));

        m_worldType.name = Fluxion_StringView_FromCStr("TestWorld");
        m_worldType.id = FLUXION_TYPE_ID_OF(TestWorld);
        m_worldType.kind = FLUXION_TYPE_KIND_STRUCT;
        m_worldType.size = 0;
        m_worldType.version = 1;
        m_worldType.members = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionPropertyInfo));
        m_worldType.methods = Fluxion_Span_Make(m_worldMethods.data(), m_worldMethods.size(), sizeof(FluxionMethodInfo));
    }

    EngineSurface(const EngineSurface&) = delete;
    EngineSurface& operator=(const EngineSurface&) = delete;

    const FluxionTypeInfo& NodeType() const { return m_nodeType; }
    const FluxionTypeInfo& WorldType() const { return m_worldType; }

private:
    FluxionTypeId m_addParameters[2]{};
    FluxionTypeId m_blendParameters[2]{};
    FluxionTypeId m_setParameters[1]{};
    FluxionTypeId m_spawnParameters[1]{};
    FluxionTypeId m_handleParameters[1]{};
    FluxionTypeId m_twoHandleParameters[2]{};

    std::vector<FluxionMethodInfo> m_nodeMethods;
    std::vector<FluxionMethodInfo> m_worldMethods;

    FluxionTypeInfo m_nodeType{};
    FluxionTypeInfo m_worldType{};
};

// --- Compiling and running against that surface ------------------------

class BoundRun
{
public:
    BoundRun(TestContext& ctx, const char* label, const std::string& source, const BindingTable& bindings)
        : m_ctx(ctx), m_label(label)
    {
        DiagnosticList diagnostics;
        CompileOptions options;
        options.fileName = label;
        options.bindings = &bindings;

        auto compiled = Compile(source, options, diagnostics);
        if (!compiled.IsOk())
        {
            std::fprintf(stderr, "  FAIL: '%s' did not compile\n", label);
            ReportDiagnostics(diagnostics);
            m_ctx.failures++;
            return;
        }

        m_module = compiled.Value();
        m_vm = CreateVm(m_module, diagnostics, &bindings);
        if (!m_vm)
        {
            std::fprintf(stderr, "  FAIL: '%s' did not load\n", label);
            ReportDiagnostics(diagnostics);
            m_ctx.failures++;
        }
    }

    ~BoundRun() { DestroyVm(m_vm); }

    BoundRun(const BoundRun&) = delete;
    BoundRun& operator=(const BoundRun&) = delete;

    bool Ready() const { return m_vm != nullptr; }
    Vm* Machine() const { return m_vm; }
    const CompiledModule& Module() const { return m_module; }

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
    CompiledModule m_module;
    Vm* m_vm = nullptr;
};

// Rejected with a message that says where, which is the whole point of
// rejecting it rather than mis-binding it.
void ExpectRejected(TestContext& ctx, const char* label, const std::string& source, const BindingTable& bindings)
{
    DiagnosticList diagnostics;
    CompileOptions options;
    options.fileName = "<test>";
    options.bindings = &bindings;

    auto compiled = Compile(source, options, diagnostics);
    if (compiled.IsOk())
    {
        std::fprintf(stderr, "  FAIL: expected '%s' to be rejected\n", label);
        ctx.failures++;
        return;
    }

    const Diagnostic* error = nullptr;
    for (const Diagnostic& entry : diagnostics.entries)
    {
        if (entry.severity == DiagnosticSeverity::Error) { error = &entry; break; }
    }

    if (!error)
    {
        std::fprintf(stderr, "  FAIL: '%s' was rejected without a diagnostic\n", label);
        ctx.failures++;
        return;
    }
    if (error->location.file != "<test>" || error->location.line == 0 || error->location.column == 0)
    {
        std::fprintf(stderr, "  FAIL: '%s' reported at %s:%u:%u\n", label, error->location.file.c_str(),
            error->location.line, error->location.column);
        ctx.failures++;
    }
}

} // namespace

void Test_Binding_Run(TestContext& ctx)
{
    EngineSurface surface;

    // The table is built from what the reflection registry holds, and is
    // handed to each compilation and each machine explicitly -- nothing
    // here is reachable through a table this module keeps for itself.
    Fluxion_Reflection_Init();
    TEST_CHECK(ctx, Fluxion_Reflection_RegisterType(&surface.NodeType()));
    TEST_CHECK(ctx, Fluxion_Reflection_RegisterType(&surface.WorldType()));

    const FluxionTypeInfo* registeredNode = Fluxion_Reflection_FindTypeByName(Fluxion_StringView_FromCStr("TestNode"));
    const FluxionTypeInfo* registeredWorld = Fluxion_Reflection_FindTypeByName(Fluxion_StringView_FromCStr("TestWorld"));
    TEST_CHECK(ctx, registeredNode != nullptr && registeredWorld != nullptr);

    BindingTable bindings;
    {
        DiagnosticList diagnostics;
        const u32 nodeIndex = registeredNode ? AddBoundType(bindings, *registeredNode, &ResolveTestNode, nullptr, diagnostics) : kNoBoundType;
        const u32 worldIndex = registeredWorld ? AddBoundType(bindings, *registeredWorld, nullptr, nullptr, diagnostics) : kNoBoundType;

        if (nodeIndex == kNoBoundType || worldIndex == kNoBoundType) ReportDiagnostics(diagnostics);
        TEST_CHECK(ctx, nodeIndex == 0);
        TEST_CHECK(ctx, worldIndex == 1);
        TEST_CHECK(ctx, bindings.types.size() == 2);

        // What the script can call is exactly what the type declared, and
        // the shapes came off the reflected descriptors rather than being
        // written out again here.
        TEST_CHECK(ctx, FindBoundMethod(bindings, 0, "Add") == 0);
        TEST_CHECK(ctx, FindBoundMethod(bindings, 0, "Missing") == kNoBoundMethod);
        if (nodeIndex == 0 && bindings.types[0].methods.size() == 4)
        {
            const BoundMethod& add = bindings.types[0].methods[0];
            TEST_CHECK(ctx, add.isInstance);
            TEST_CHECK(ctx, add.parameterTypes.size() == 2);
            TEST_CHECK(ctx, add.parameterTypes[0] == ValueType::Int && add.parameterTypes[1] == ValueType::Int);
            TEST_CHECK(ctx, add.returnType == ValueType::Int);
        }
        if (worldIndex == 1 && bindings.types[1].methods.size() == 4)
        {
            const BoundMethod& spawn = bindings.types[1].methods[0];
            TEST_CHECK(ctx, !spawn.isInstance);
            TEST_CHECK(ctx, spawn.returnType == ValueType::Handle);
            TEST_CHECK(ctx, spawn.returnBoundType == 0);

            const BoundMethod& valueOf = bindings.types[1].methods[2];
            TEST_CHECK(ctx, valueOf.parameterTypes.size() == 1);
            TEST_CHECK(ctx, valueOf.parameterTypes[0] == ValueType::Handle);
            TEST_CHECK(ctx, valueOf.parameterBoundTypes[0] == 0);
        }
    }

    {
        // Handles kept in an array must come back out unchanged. An array
        // of them is one object as far as the collector is concerned, and
        // its elements must not be walked as if they named script objects.
        BoundRun run(ctx, "handles-round-trip-through-an-array",
            "static class Program\n"
            "{\n"
            "    static int Main()\n"
            "    {\n"
            "        TestNode[] nodes = new TestNode[4];\n"
            "        for (int i = 0; i < 4; i += 1) { nodes[i] = TestWorld.Spawn(i + 1); }\n"
            "        Gc.Collect();\n"
            "        int total = 0;\n"
            "        foreach (TestNode n in nodes) { total += TestWorld.ValueOf(n); }\n"
            "        return total;\n"
            "    }\n"
            "}\n",
            bindings);
        ScriptValue value;
        if (run.Ready() && run.Call("Program.Main", value))
        {
            TEST_CHECK(ctx, value.intValue == 10); // 1 + 2 + 3 + 4
        }
    }
    {
        // The engine object behind a handle stays the engine's business:
        // a collection that reclaims the script object holding the handle
        // must not disturb it, and the handle must still resolve.
        EngineHandle kept{};
        {
            BoundRun run(ctx, "handle-field-survives-owner-collection",
                "class Carrier { TestNode node; Carrier(TestNode n) { this.node = n; } TestNode Get() { return this.node; } }\n"
                "static class Program\n"
                "{\n"
                "    static TestNode Main()\n"
                "    {\n"
                "        Carrier keeper = new Carrier(TestWorld.Spawn(99));\n"
                "        for (int i = 0; i < 300; i += 1) { Carrier junk = new Carrier(TestWorld.Spawn(1)); }\n"
                "        Gc.Collect();\n"
                "        return keeper.Get();\n"
                "    }\n"
                "}\n",
                bindings);
            ScriptValue value;
            if (run.Ready() && run.Call("Program.Main", value))
            {
                TEST_CHECK(ctx, value.type == ValueType::Handle);
                kept = value.handleValue;
                TestNode* node = Store().Resolve(kept);
                TEST_CHECK(ctx, node != nullptr);
                if (node) TEST_CHECK(ctx, node->value == 99);
            }
        }
        // Tearing the machine down does not reach into engine-owned data.
        TEST_CHECK(ctx, Store().IsAlive(kept));
    }
    {
        // A handle the engine has retired must report a fault when it is
        // used, not silently resolve to whatever now occupies the slot.
        EngineHandle stale = Store().Spawn(5);
        Store().Retire(stale);
        TEST_CHECK(ctx, !Store().IsAlive(stale));
    }
    {
        // A method of a reflected C++ type, called from script with two
        // arguments and answering with a value the script then uses.
        BoundRun run(ctx, "engine-method-with-arguments",
            "static class Program\n"
            "{\n"
            "    static int Main()\n"
            "    {\n"
            "        TestNode n = TestWorld.Spawn(10);\n"
            "        int afterFirst = n.Add(3, 4);\n"
            "        int afterSecond = n.Add(1, 2);\n"
            "        return afterFirst * 1000 + afterSecond;\n"
            "    }\n"
            "}\n",
            bindings);

        ScriptValue value;
        if (run.Ready() && run.Call("Program.Main", value))
        {
            // 10 + 7 = 17, then 17 + 3 = 20: both calls reached the same
            // engine object, and each argument landed where it belonged.
            TEST_CHECK(ctx, value.type == ValueType::Int);
            TEST_CHECK(ctx, value.intValue == 17020);
        }
    }
    {
        // A value comes back as a float, and an int argument widens into
        // a float parameter on the way in -- the language's one implicit
        // conversion, applied inside a multi-argument call.
        BoundRun run(ctx, "engine-method-returns-and-widens",
            "static class Program\n"
            "{\n"
            "    static float Main()\n"
            "    {\n"
            "        TestNode n = TestWorld.Spawn(0);\n"
            "        float exact = n.Blend(1.5f, 4);\n"
            "        float widened = n.Blend(2, 3);\n"
            "        return exact + widened;\n"
            "    }\n"
            "}\n",
            bindings);

        ScriptValue value;
        if (run.Ready() && run.Call("Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Float);
            TEST_CHECK(ctx, value.floatValue == 12.0f); // 6.0 + 6.0
        }
    }
    {
        // A handle the script received from one call is handed to
        // another, which sees the same handle it was given.
        BoundRun run(ctx, "handle-round-trip",
            "static class Program\n"
            "{\n"
            "    static bool Main()\n"
            "    {\n"
            "        TestNode a = TestWorld.Spawn(41);\n"
            "        TestNode b = TestWorld.Echo(a);\n"
            "        bool sameToTheEngine = TestWorld.Same(a, b);\n"
            "        bool sameToTheScript = a == b;\n"
            "        bool readsBack = TestWorld.ValueOf(b) == 41;\n"
            "        return sameToTheEngine && sameToTheScript && readsBack;\n"
            "    }\n"
            "}\n",
            bindings);

        ScriptValue value;
        if (run.Ready() && run.Call("Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Bool);
            TEST_CHECK(ctx, value.boolValue);
        }
    }
    {
        // A handle handed back to the host is the engine's own, and still
        // names something the engine knows.
        BoundRun run(ctx, "handle-reaches-the-host",
            "static class Program\n"
            "{\n"
            "    static TestNode Main() { return TestWorld.Spawn(77); }\n"
            "}\n",
            bindings);

        ScriptValue value;
        if (run.Ready() && run.Call("Program.Main", value))
        {
            TEST_CHECK(ctx, value.type == ValueType::Handle);
            TEST_CHECK(ctx, Store().IsAlive(value.handleValue));

            TestNode* node = Store().Resolve(value.handleValue);
            TEST_CHECK(ctx, node != nullptr && node->value == 77);
        }
    }
    {
        // A handle whose engine object has gone is refused when it is
        // used, rather than reaching whatever occupies that place now.
        BoundRun run(ctx, "stale-handle-is-refused",
            "static class Program\n"
            "{\n"
            "    static TestNode Make() { return TestWorld.Spawn(5); }\n"
            "    static int Touch(TestNode n) { return n.Read(); }\n"
            "}\n",
            bindings);

        ScriptValue handle;
        if (run.Ready() && run.Call("Program.Make", handle))
        {
            const u32 touch = FindMethod(run.Machine(), FindClass(run.Machine(), "Program"), "Touch");
            TEST_CHECK(ctx, touch != kNoFunction);

            auto beforeRetiring = InvokeStatic(run.Machine(), touch, &handle, 1);
            TEST_CHECK(ctx, beforeRetiring.IsOk());
            TEST_CHECK(ctx, beforeRetiring.IsOk() && beforeRetiring.Value().intValue == 5);

            Store().Retire(handle.handleValue);

            auto afterRetiring = InvokeStatic(run.Machine(), touch, &handle, 1);
            TEST_CHECK(ctx, !afterRetiring.IsOk());
            TEST_CHECK(ctx, GetFaultDetail(run.Machine()).faulted);
            TEST_CHECK(ctx, !GetFaultDetail(run.Machine()).frames.empty());
        }
    }

    // --- A handle is not something the collector follows ----------------

    {
        // The bitmap a class carries decides what a collection follows,
        // and a handle-typed field must not be in it: the object behind
        // it belongs to the engine, not to the script heap.
        DiagnosticList diagnostics;
        CompileOptions options;
        options.fileName = "handle-fields-are-not-references";
        options.bindings = &bindings;

        auto compiled = Compile(
            "class Holder\n"
            "{\n"
            "    TestNode node;\n"
            "    int count;\n"
            "    Holder(TestNode n) { this.node = n; this.count = 1; }\n"
            "    TestNode Get() { return this.node; }\n"
            "}\n"
            "static class Program\n"
            "{\n"
            "    static int Main()\n"
            "    {\n"
            "        Holder h = new Holder(TestWorld.Spawn(1));\n"
            "        TestNode local = h.Get();\n"
            "        return h.count;\n"
            "    }\n"
            "}\n",
            options, diagnostics);

        TEST_CHECK(ctx, compiled.IsOk());
        if (compiled.IsOk())
        {
            const ClassInfo* holder = nullptr;
            for (const ClassInfo& info : compiled.Value().classes)
            {
                if (info.name == "Holder") holder = &info;
            }
            TEST_CHECK(ctx, holder != nullptr);
            if (holder)
            {
                TEST_CHECK(ctx, holder->fieldSlotCount == 2);
                TEST_CHECK(ctx, !IsReferenceBitSet(holder->fieldReferenceBits, 0));
                TEST_CHECK(ctx, !IsReferenceBitSet(holder->fieldReferenceBits, 1));
            }

            // And the same for a frame slot: the local holding a handle
            // is not a root either.
            const FunctionInfo* main = nullptr;
            for (const FunctionInfo& function : compiled.Value().functions)
            {
                if (function.qualifiedName == "Program.Main") main = &function;
            }
            TEST_CHECK(ctx, main != nullptr);
            if (main)
            {
                bool anyReference = false;
                for (u32 slot = 0; slot < main->localSlotCount; ++slot)
                {
                    if (IsReferenceBitSet(main->localReferenceBits, slot)) anyReference = true;
                }
                // Exactly one root: the Holder. The handle beside it is a
                // value like any other.
                TEST_CHECK(ctx, anyReference);
                TEST_CHECK(ctx, IsReferenceBitSet(main->localReferenceBits, 0));
                TEST_CHECK(ctx, !IsReferenceBitSet(main->localReferenceBits, 1));
            }
        }
    }
    {
        // The numbers a handle is made of are exactly the numbers a live
        // script reference is made of. Allocating and collecting heavily
        // with such a handle held in a field must neither keep script
        // objects alive nor disturb what the engine has.
        const EngineHandle beforeRun = Store().Spawn(4242);

        BoundRun run(ctx, "handles-are-invisible-to-the-collector",
            "class Holder\n"
            "{\n"
            "    TestNode node;\n"
            "    Holder(TestNode n) { this.node = n; }\n"
            "    TestNode Get() { return this.node; }\n"
            "}\n"
            "class Junk { int a; int b; Junk(int v) { this.a = v; this.b = v; } }\n"
            "static class Waste\n"
            "{\n"
            "    static int Churn(int n)\n"
            "    {\n"
            "        for (int i = 0; i < n; i += 1) { Junk t = new Junk(i); }\n"
            "        return n;\n"
            "    }\n"
            "}\n"
            "static class Program\n"
            "{\n"
            "    static int Main()\n"
            "    {\n"
            "        Holder held = new Holder(TestWorld.Spawn(99));\n"
            "        Waste.Churn(2000);\n"
            "        Gc.Collect();\n"
            "        if (TestWorld.ValueOf(held.Get()) != 99) { return 0 - 1; }\n"
            "        return Gc.LiveObjects();\n"
            "    }\n"
            "}\n",
            bindings);

        ScriptValue value;
        if (run.Ready() && run.Call("Program.Main", value))
        {
            // The Holder alone. Every one of the two thousand throwaway
            // objects went away, so nothing was kept alive on account of
            // a handle's numeric value looking like a reference.
            TEST_CHECK(ctx, value.intValue == 1);

            const HeapStats stats = GetHeapStats(run.Machine());
            TEST_CHECK(ctx, stats.totalAllocations == 2001);
            TEST_CHECK(ctx, stats.collectionCount > 0);

            // And the engine's own objects are untouched: one made before
            // the script ran at all, and one the script asked for.
            TEST_CHECK(ctx, Store().IsAlive(beforeRun));
            TestNode* node = Store().Resolve(beforeRun);
            TEST_CHECK(ctx, node != nullptr && node->value == 4242);
        }
    }

    // --- Reaching a class and its methods by index ----------------------

    {
        BoundRun run(ctx, "runtime-class-and-method-access",
            "class Counter\n"
            "{\n"
            "    int total;\n"
            "    string label;\n"
            "\n"
            "    Counter(int start, string name) { this.total = start; this.label = name; }\n"
            "\n"
            "    int Bump(int by) { this.total = this.total + by; return this.total; }\n"
            "    string Describe() { return this.label; }\n"
            "    virtual int Scaled(int by) { return this.total * by; }\n"
            "}\n"
            "class DoubleCounter : Counter\n"
            "{\n"
            "    DoubleCounter(int start, string name) : base(start, name) { }\n"
            "    override int Scaled(int by) { return this.total * by * 2; }\n"
            "}\n"
            "static class Program\n"
            "{\n"
            "    static int Unused() { return 0; }\n"
            "}\n",
            bindings);

        if (run.Ready())
        {
            Vm* vm = run.Machine();

            const u32 counterClass = FindClass(vm, "Counter");
            const u32 doubleClass = FindClass(vm, "DoubleCounter");
            TEST_CHECK(ctx, counterClass != kNoClass);
            TEST_CHECK(ctx, doubleClass != kNoClass);
            TEST_CHECK(ctx, FindClass(vm, "NoSuchClass") == kNoClass);

            // Resolved once, then used with no name lookup at all.
            const u32 bump = FindMethod(vm, counterClass, "Bump");
            const u32 describe = FindMethod(vm, counterClass, "Describe");
            const u32 scaled = FindMethod(vm, counterClass, "Scaled");
            TEST_CHECK(ctx, bump != kNoFunction && describe != kNoFunction && scaled != kNoFunction);
            TEST_CHECK(ctx, FindMethod(vm, counterClass, "NoSuchMethod") == kNoFunction);

            TEST_CHECK(ctx, MethodName(vm, bump) != nullptr && std::string(MethodName(vm, bump)) == "Counter.Bump");
            TEST_CHECK(ctx, ClassName(vm, counterClass) != nullptr && std::string(ClassName(vm, counterClass)) == "Counter");

            ScriptValue start;
            start.type = ValueType::Int;
            start.intValue = 5;

            ScriptValue name;
            name.type = ValueType::String;
            name.stringValue = "counter";

            const ScriptValue constructorArgs[2] = { start, name };

            auto made = NewInstance(vm, counterClass, constructorArgs, 2);
            TEST_CHECK(ctx, made.IsOk());
            if (made.IsOk())
            {
                const ObjectHandle instance = made.Value();
                TEST_CHECK(ctx, IsObjectAlive(vm, instance));

                // The constructor's arguments actually arrived.
                auto described = InvokeMethod(vm, instance, describe, nullptr, 0);
                TEST_CHECK(ctx, described.IsOk());
                TEST_CHECK(ctx, described.IsOk() && described.Value().stringValue == "counter");

                ScriptValue by;
                by.type = ValueType::Int;
                by.intValue = 3;

                auto bumped = InvokeMethod(vm, instance, bump, &by, 1);
                TEST_CHECK(ctx, bumped.IsOk());
                TEST_CHECK(ctx, bumped.IsOk() && bumped.Value().intValue == 8);

                auto bumpedAgain = InvokeMethod(vm, instance, bump, &by, 1);
                TEST_CHECK(ctx, bumpedAgain.IsOk() && bumpedAgain.Value().intValue == 11);

                // Wrong number of arguments, and an argument of the wrong
                // type, are both refused rather than read as something
                // they are not.
                TEST_CHECK(ctx, !InvokeMethod(vm, instance, bump, nullptr, 0).IsOk());
                TEST_CHECK(ctx, !InvokeMethod(vm, instance, bump, &name, 1).IsOk());
            }

            // The method is named on the base class, but which body runs
            // is decided by the instance it is called on.
            auto derived = NewInstance(vm, doubleClass, constructorArgs, 2);
            TEST_CHECK(ctx, derived.IsOk());
            if (derived.IsOk())
            {
                ScriptValue by;
                by.type = ValueType::Int;
                by.intValue = 4;

                auto result = InvokeMethod(vm, derived.Value(), scaled, &by, 1);
                TEST_CHECK(ctx, result.IsOk());
                TEST_CHECK(ctx, result.IsOk() && result.Value().intValue == 40); // 5 * 4 * 2
            }
        }
    }

    // --- The same, driven entirely through the service table ------------

    {
        Fluxion_ServiceRegistry_Init();

        ScriptRuntimeService service = MakeScriptRuntimeService();
        TEST_CHECK(ctx, Fluxion::Core::RegisterService(service));

        const ScriptRuntimeService* found = Fluxion::Core::GetService<ScriptRuntimeService>();
        TEST_CHECK(ctx, found == &service);

        if (found)
        {
            TEST_CHECK(ctx, found->header.version == ScriptRuntimeService::Version);
            TEST_CHECK(ctx, found->header.structSize == sizeof(ScriptRuntimeService));

            DiagnosticList diagnostics;
            CompileOptions options;
            options.fileName = "<service>";
            options.bindings = &bindings;

            CompiledModule module;
            const bool compiled = found->compile(
                "class Greeter\n"
                "{\n"
                "    int seed;\n"
                "    Greeter(int start) { this.seed = start; }\n"
                "    int Twice(int by) { return this.seed * by * 2; }\n"
                "}\n"
                "static class Program\n"
                "{\n"
                "    static int Main() { return TestWorld.ValueOf(TestWorld.Spawn(31)); }\n"
                "}\n",
                &options, &diagnostics, &module);

            if (!compiled) ReportDiagnostics(diagnostics);
            TEST_CHECK(ctx, compiled);

            if (compiled)
            {
                Vm* vm = found->createVm(&module, &bindings, &diagnostics);
                if (!vm) ReportDiagnostics(diagnostics);
                TEST_CHECK(ctx, vm != nullptr);

                if (vm)
                {
                    ScriptValue value;
                    TEST_CHECK(ctx, found->invoke(vm, "Program.Main", &value));
                    TEST_CHECK(ctx, value.intValue == 31);

                    const u32 greeter = found->findClass(vm, "Greeter");
                    TEST_CHECK(ctx, greeter != kNoClass);

                    const u32 twice = found->findMethod(vm, greeter, "Twice");
                    TEST_CHECK(ctx, twice != kNoFunction);

                    ScriptValue seed;
                    seed.type = ValueType::Int;
                    seed.intValue = 6;

                    ObjectHandle instance;
                    TEST_CHECK(ctx, found->newInstance(vm, greeter, &seed, 1, &instance));

                    ScriptValue by;
                    by.type = ValueType::Int;
                    by.intValue = 5;

                    ScriptValue result;
                    TEST_CHECK(ctx, found->invokeMethod(vm, instance, twice, &by, 1, &result));
                    TEST_CHECK(ctx, result.intValue == 60);

                    found->destroyVm(vm);
                }
            }
        }

        Fluxion::Core::UnregisterService<ScriptRuntimeService>();
        TEST_CHECK(ctx, Fluxion::Core::GetService<ScriptRuntimeService>() == nullptr);
        Fluxion_ServiceRegistry_Shutdown();
    }

    // --- What must not be accepted --------------------------------------

    {
        // An engine type is not something a script creates, and a handle
        // is not a reference: neither `new` nor `null` applies to one.
        ExpectRejected(ctx, "new on an engine type",
            "static class Program\n"
            "{\n"
            "    static int Main() { TestNode n = new TestNode(); return 0; }\n"
            "}\n",
            bindings);

        ExpectRejected(ctx, "a handle compared with null",
            "static class Program\n"
            "{\n"
            "    static bool Main() { TestNode n = TestWorld.Spawn(1); return n == null; }\n"
            "}\n",
            bindings);

        ExpectRejected(ctx, "a handle assigned null",
            "static class Program\n"
            "{\n"
            "    static int Main() { TestNode n = null; return 0; }\n"
            "}\n",
            bindings);

        ExpectRejected(ctx, "an engine method given the wrong argument type",
            "static class Program\n"
            "{\n"
            "    static int Main() { TestNode n = TestWorld.Spawn(1); return n.Add(1, true); }\n"
            "}\n",
            bindings);

        ExpectRejected(ctx, "an engine method given too few arguments",
            "static class Program\n"
            "{\n"
            "    static int Main() { TestNode n = TestWorld.Spawn(1); return n.Add(1); }\n"
            "}\n",
            bindings);

        ExpectRejected(ctx, "an instance method called through the type name",
            "static class Program\n"
            "{\n"
            "    static int Main() { return TestNode.Add(1, 2); }\n"
            "}\n",
            bindings);

        ExpectRejected(ctx, "a static method called through a handle",
            "static class Program\n"
            "{\n"
            "    static int Main() { TestNode n = TestWorld.Spawn(1); return n.Spawn(2); }\n"
            "}\n",
            bindings);

        ExpectRejected(ctx, "a method the engine type does not have",
            "static class Program\n"
            "{\n"
            "    static int Main() { TestNode n = TestWorld.Spawn(1); return n.Nope(); }\n"
            "}\n",
            bindings);

        // Nothing was made visible, so nothing outside the source can be
        // named -- the table is what decides, not the module.
        BindingTable nothing;
        ExpectRejected(ctx, "an engine type no table was given",
            "static class Program\n"
            "{\n"
            "    static int Main() { TestNode n = TestWorld.Spawn(1); return 0; }\n"
            "}\n",
            nothing);
    }

    // --- Built-ins, now chosen by the whole argument list ---------------

    {
        // Each entry names the list it accepts, and the eighteen the
        // language has still take what they always took.
        const NativeFunctionSignature* natives = NativeFunctionTable();
        TEST_CHECK(ctx, kNativeFunctionCount == 18);

        u32 withOne = 0;
        u32 withNone = 0;
        for (u32 i = 0; i < kNativeFunctionCount; ++i)
        {
            if (natives[i].parameterCount == 0) { ++withNone; TEST_CHECK(ctx, natives[i].parameterTypes == nullptr); }
            else { ++withOne; TEST_CHECK(ctx, natives[i].parameterCount == 1 && natives[i].parameterTypes != nullptr); }
        }
        TEST_CHECK(ctx, withOne == 16 && withNone == 2);
    }
    {
        // An exact match is taken over one reached by conversion: an int
        // argument picks the form written for ints even though a form
        // taking a float is equally in reach.
        DiagnosticList diagnostics;
        CompileOptions options;
        options.fileName = "exact-match-wins";

        auto compiled = Compile(
            "static class Program\n"
            "{\n"
            "    static void Main()\n"
            "    {\n"
            "        Console.WriteLine(7);\n"
            "        Console.WriteLine(7.5f);\n"
            "        Console.WriteLine(true);\n"
            "        Gc.Collect();\n"
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
                std::vector<u32> chosen;
                bool anyConversion = false;
                for (u32 i = 0; i < main->codeLength; ++i)
                {
                    const Instruction instruction = compiled.Value().code[main->codeOffset + i];
                    if (instruction.op == OpCode::CallNative) chosen.push_back(instruction.operand);
                    if (instruction.op == OpCode::IntToFloat) anyConversion = true;
                }

                TEST_CHECK(ctx, chosen.size() == 4);
                if (chosen.size() == 4)
                {
                    TEST_CHECK(ctx, chosen[0] == (u32)NativeFunctionId::ConsoleWriteLineInt);
                    TEST_CHECK(ctx, chosen[1] == (u32)NativeFunctionId::ConsoleWriteLineFloat);
                    TEST_CHECK(ctx, chosen[2] == (u32)NativeFunctionId::ConsoleWriteLineBool);
                    TEST_CHECK(ctx, chosen[3] == (u32)NativeFunctionId::GcCollect);
                }
                TEST_CHECK(ctx, !anyConversion);
            }
        }
    }
    {
        // An argument list nothing accepts is a compile error, reported
        // where it was written.
        ExpectRejected(ctx, "a built-in given a type it does not take",
            "static class Program\n"
            "{\n"
            "    static void Main() { Debug.Log(7); }\n"
            "}\n",
            bindings);

        ExpectRejected(ctx, "a built-in given too many arguments",
            "static class Program\n"
            "{\n"
            "    static void Main() { Console.WriteLine(1, 2); }\n"
            "}\n",
            bindings);

        ExpectRejected(ctx, "a built-in given an argument it takes none of",
            "static class Program\n"
            "{\n"
            "    static void Main() { Gc.Collect(1); }\n"
            "}\n",
            bindings);

        ExpectRejected(ctx, "a built-in given nothing when it takes something",
            "static class Program\n"
            "{\n"
            "    static void Main() { Console.WriteLine(); }\n"
            "}\n",
            bindings);
    }

    Fluxion_Reflection_Shutdown();
}
