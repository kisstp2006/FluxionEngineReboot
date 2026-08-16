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
