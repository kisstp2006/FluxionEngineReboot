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

// Each literal below is exactly one line of the source, so the line
// numbers asserted further down can be read straight off this text: the
// first literal is line 1.
//
// The fault is an int divided by zero, which nothing folds away at
// compile time: it happens at the bottom of a four-deep chain of calls,
// which is what makes the stack worth looking at.
const char* const kFaultingChain =
    "static class Program\n"           // 1
    "{\n"                              // 2
    "    static int Innermost(int n)\n" // 3
    "    {\n"                          // 4
    "        return n / 0;\n"          // 5
    "    }\n"                          // 6
    "\n"                               // 7
    "    static int Middle(int n)\n"   // 8
    "    {\n"                          // 9
    "        return Innermost(n);\n"   // 10
    "    }\n"                          // 11
    "\n"                               // 12
    "    static int Outer(int n)\n"    // 13
    "    {\n"                          // 14
    "        return Middle(n);\n"      // 15
    "    }\n"                          // 16
    "\n"                               // 17
    "    static int Main()\n"          // 18
    "    {\n"                          // 19
    "        return Outer(5);\n"       // 20
    "    }\n"                          // 21
    "}\n";                             // 22

// A method on an instance so the reported name is the qualified one and
// not merely the method's own.
const char* const kFaultingInstance =
    "class Reader\n"                                    // 1
    "{\n"                                               // 2
    "    int[] slots;\n"                                // 3
    "\n"                                                // 4
    "    Reader() { this.slots = new int[2]; }\n"       // 5
    "\n"                                                // 6
    "    int Read(int at)\n"                            // 7
    "    {\n"                                           // 8
    "        return this.slots[at];\n"                  // 9
    "    }\n"                                           // 10
    "}\n"                                               // 11
    "\n"                                                // 12
    "static class Program\n"                            // 13
    "{\n"                                               // 14
    "    static int Main()\n"                           // 15
    "    {\n"                                           // 16
    "        Reader r = new Reader();\n"                // 17
    "        return r.Read(7);\n"                       // 18
    "    }\n"                                           // 19
    "}\n";                                              // 20

class Loaded
{
public:
    Loaded(TestContext& ctx, const char* label, const std::string& source)
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

        m_module = compiled.Value();
        m_vm = CreateVm(m_module, diagnostics);
        if (!m_vm)
        {
            std::fprintf(stderr, "  FAIL: '%s' did not load\n", label);
            ReportDiagnostics(diagnostics);
            m_ctx.failures++;
        }
    }

    ~Loaded() { DestroyVm(m_vm); }

    Loaded(const Loaded&) = delete;
    Loaded& operator=(const Loaded&) = delete;

    bool Ready() const { return m_vm != nullptr; }
    Vm* Machine() const { return m_vm; }
    const CompiledModule& Module() const { return m_module; }

private:
    TestContext& m_ctx;
    const char* m_label;
    CompiledModule m_module;
    Vm* m_vm = nullptr;
};

} // namespace

void Test_StackTrace_Run(TestContext& ctx)
{
    {
        // Every instruction knows which line it came from, and there is
        // exactly one such answer per instruction.
        Loaded loaded(ctx, "<lines>", kFaultingChain);
        if (loaded.Ready())
        {
            const CompiledModule& module = loaded.Module();
            TEST_CHECK(ctx, module.sourceLines.size() == module.code.size());

            bool everyLineIsSet = true;
            for (u32 line : module.sourceLines) everyLineIsSet = everyLineIsSet && line != 0;
            TEST_CHECK(ctx, everyLineIsSet);

            // A method's body belongs to the file it was written in, and
            // the prelude's methods to the prelude.
            bool named = false;
            for (const FunctionInfo& function : module.functions)
            {
                if (function.qualifiedName != "Program.Main") continue;
                named = true;
                TEST_CHECK(ctx, function.sourceFile == "<lines>");
            }
            TEST_CHECK(ctx, named);
        }
    }
    {
        // A fault four calls deep leaves one entry per active call,
        // innermost first, each saying where in which file it was.
        Loaded loaded(ctx, "<chain>", kFaultingChain);
        if (loaded.Ready())
        {
            auto result = Invoke(loaded.Machine(), "Program.Main");
            TEST_CHECK(ctx, !result.IsOk());

            const FaultDetail& fault = GetFaultDetail(loaded.Machine());
            TEST_CHECK(ctx, fault.faulted);
            TEST_CHECK(ctx, fault.code == result.Status().code);
            TEST_CHECK(ctx, fault.message != nullptr);
            TEST_CHECK(ctx, fault.frames.size() == 4);

            if (fault.frames.size() == 4)
            {
                TEST_CHECK(ctx, fault.frames[0].method == "Program.Innermost");
                TEST_CHECK(ctx, fault.frames[1].method == "Program.Middle");
                TEST_CHECK(ctx, fault.frames[2].method == "Program.Outer");
                TEST_CHECK(ctx, fault.frames[3].method == "Program.Main");

                for (const FaultFrame& frame : fault.frames)
                {
                    TEST_CHECK(ctx, frame.file == "<chain>");
                    TEST_CHECK(ctx, frame.line != 0);
                }

                // The innermost frame is at the division; each caller is
                // at the line its own call was written on.
                TEST_CHECK(ctx, fault.frames[0].line == 5);
                TEST_CHECK(ctx, fault.frames[1].line == 10);
                TEST_CHECK(ctx, fault.frames[2].line == 15);
                TEST_CHECK(ctx, fault.frames[3].line == 20);
            }
        }
    }
    {
        // A method reached through an instance is named the same way a
        // call site names it, and a fault in it reports the line inside
        // the method rather than the line of the call.
        Loaded loaded(ctx, "<instance>", kFaultingInstance);
        if (loaded.Ready())
        {
            auto result = Invoke(loaded.Machine(), "Program.Main");
            TEST_CHECK(ctx, !result.IsOk());

            const FaultDetail& fault = GetFaultDetail(loaded.Machine());
            TEST_CHECK(ctx, fault.faulted);
            TEST_CHECK(ctx, fault.frames.size() == 2);
            if (fault.frames.size() == 2)
            {
                TEST_CHECK(ctx, fault.frames[0].method == "Reader.Read");
                TEST_CHECK(ctx, fault.frames[0].line == 9);
                TEST_CHECK(ctx, fault.frames[1].method == "Program.Main");
                TEST_CHECK(ctx, fault.frames[1].line == 18);
            }
        }
    }
    {
        // Nothing went wrong, so there is nothing to report: a run that
        // finishes leaves the detail empty rather than leaving the
        // previous fault standing.
        Loaded loaded(ctx, "<quiet>",
            "static class Program\n"
            "{\n"
            "    static int Bad() { return 1 / 0; }\n"
            "    static int Good() { return 7; }\n"
            "}\n");
        if (loaded.Ready())
        {
            auto bad = Invoke(loaded.Machine(), "Program.Bad");
            TEST_CHECK(ctx, !bad.IsOk());
            TEST_CHECK(ctx, GetFaultDetail(loaded.Machine()).faulted);

            auto good = Invoke(loaded.Machine(), "Program.Good");
            TEST_CHECK(ctx, good.IsOk());
            TEST_CHECK(ctx, good.IsOk() && good.Value().intValue == 7);
            TEST_CHECK(ctx, !GetFaultDetail(loaded.Machine()).faulted);
            TEST_CHECK(ctx, GetFaultDetail(loaded.Machine()).frames.empty());
        }
    }
}
