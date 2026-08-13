#include "TestFramework.h"

#include <Fluxion/Script/Script.hpp>

#include <cstdio>
#include <string>

using namespace Fluxion::Script;

namespace
{

const char* const kAddSource =
    "static class Program\n"
    "{\n"
    "    static int Add(int a, int b)\n"
    "    {\n"
    "        return a + b;\n"
    "    }\n"
    "}\n";

const FunctionInfo* FindFunction(const CompiledModule& module, const char* qualifiedName)
{
    for (const FunctionInfo& function : module.functions)
    {
        if (function.qualifiedName == qualifiedName) return &function;
    }
    return nullptr;
}

void ReportDiagnostics(const DiagnosticList& diagnostics)
{
    for (const Diagnostic& entry : diagnostics.entries)
        std::fprintf(stderr, "  %s:%u:%u: %s\n", entry.location.file.c_str(), entry.location.line, entry.location.column, entry.message.c_str());
}

} // namespace

void Test_Bytecode_Run(TestContext& ctx)
{
    DiagnosticList diagnostics;
    CompileOptions options;
    options.fileName = "<test>";
    options.moduleVersion = 7;

    auto compiled = Compile(kAddSource, options, diagnostics);
    if (!compiled.IsOk()) ReportDiagnostics(diagnostics);
    TEST_CHECK(ctx, compiled.IsOk());
    if (!compiled.IsOk()) return;

    const CompiledModule& module = compiled.Value();

    {
        TEST_CHECK(ctx, module.header.magic[0] == 'F');
        TEST_CHECK(ctx, module.header.magic[1] == 'L');
        TEST_CHECK(ctx, module.header.magic[2] == 'X');
        TEST_CHECK(ctx, module.header.magic[3] == 'S');
        TEST_CHECK(ctx, module.header.languageVersion == kLanguageVersion);
        TEST_CHECK(ctx, module.header.bytecodeVersion == kBytecodeVersion);
        TEST_CHECK(ctx, module.header.engineAbiVersion == kEngineAbiVersion);
        TEST_CHECK(ctx, module.header.moduleVersion == 7);
        TEST_CHECK(ctx, module.sourceName == "<test>");
    }

    {
        const FunctionInfo* add = FindFunction(module, "Program.Add");
        TEST_CHECK(ctx, add != nullptr);
        if (add)
        {
            TEST_CHECK(ctx, add->returnType == ValueType::Int);
            TEST_CHECK(ctx, add->parameterTypes.size() == 2);
            TEST_CHECK(ctx, add->localSlotCount == 2);

            // Both parameters are loaded, added with the int-specific
            // opcode and returned; the trailing pair is the default
            // return every function ends with.
            TEST_CHECK(ctx, add->codeLength == 6);
            if (add->codeLength == 6)
            {
                const Instruction* code = &module.code[add->codeOffset];
                TEST_CHECK(ctx, code[0].op == OpCode::LoadLocal && code[0].operand == 0);
                TEST_CHECK(ctx, code[1].op == OpCode::LoadLocal && code[1].operand == 1);
                TEST_CHECK(ctx, code[2].op == OpCode::AddInt);
                TEST_CHECK(ctx, code[3].op == OpCode::Return);
                TEST_CHECK(ctx, code[4].op == OpCode::PushInt);
                TEST_CHECK(ctx, code[5].op == OpCode::Return);
            }
        }
    }

    {
        // Float operands select the float-specific opcodes, so the
        // interpreter never has to ask what an operand holds.
        DiagnosticList floatDiagnostics;
        CompileOptions floatOptions;
        floatOptions.fileName = "<test>";
        auto floatModule = Compile(
            "static class Program { static float Scale(float v) { return v * 2.0f; } }\n",
            floatOptions, floatDiagnostics);
        TEST_CHECK(ctx, floatModule.IsOk());
        if (floatModule.IsOk())
        {
            const FunctionInfo* scale = FindFunction(floatModule.Value(), "Program.Scale");
            TEST_CHECK(ctx, scale != nullptr);
            if (scale && scale->codeLength >= 4)
            {
                const Instruction* code = &floatModule.Value().code[scale->codeOffset];
                TEST_CHECK(ctx, code[0].op == OpCode::LoadLocal);
                TEST_CHECK(ctx, code[1].op == OpCode::PushFloat);
                TEST_CHECK(ctx, code[2].op == OpCode::MulFloat);
                TEST_CHECK(ctx, code[3].op == OpCode::Return);
            }
        }
    }

    {
        // A well-formed module loads.
        DiagnosticList loadDiagnostics;
        Vm* vm = CreateVm(module, loadDiagnostics);
        TEST_CHECK(ctx, vm != nullptr);
        TEST_CHECK(ctx, !loadDiagnostics.HasErrors());
        DestroyVm(vm);
    }

    {
        // A damaged signature is refused before a single instruction runs.
        CompiledModule damaged = module;
        damaged.header.magic[0] = 'X';

        DiagnosticList loadDiagnostics;
        Vm* vm = CreateVm(damaged, loadDiagnostics);
        TEST_CHECK(ctx, vm == nullptr);
        TEST_CHECK(ctx, loadDiagnostics.HasErrors());
        DestroyVm(vm);
    }

    {
        // So is an instruction set this build does not speak.
        CompiledModule stale = module;
        stale.header.bytecodeVersion = kBytecodeVersion + 1;

        DiagnosticList loadDiagnostics;
        Vm* vm = CreateVm(stale, loadDiagnostics);
        TEST_CHECK(ctx, vm == nullptr);
        TEST_CHECK(ctx, loadDiagnostics.HasErrors());
        DestroyVm(vm);
    }

    {
        // A module built before the instruction set gained its object
        // sections is refused just as firmly as one from the future:
        // running it would misread every opcode past the ones it knew.
        CompiledModule older = module;
        older.header.bytecodeVersion = kBytecodeVersion - 1;

        DiagnosticList loadDiagnostics;
        Vm* vm = CreateVm(older, loadDiagnostics);
        TEST_CHECK(ctx, vm == nullptr);
        TEST_CHECK(ctx, loadDiagnostics.HasErrors());
        DestroyVm(vm);
    }

    {
        CompiledModule stale = module;
        stale.header.engineAbiVersion = kEngineAbiVersion + 1;

        DiagnosticList loadDiagnostics;
        Vm* vm = CreateVm(stale, loadDiagnostics);
        TEST_CHECK(ctx, vm == nullptr);
        TEST_CHECK(ctx, loadDiagnostics.HasErrors());
        DestroyVm(vm);
    }

    {
        // Compilation stops at the first step that fails, and says which.
        DiagnosticList badDiagnostics;
        CompileOptions badOptions;
        badOptions.fileName = "<test>";
        auto broken = Compile("static class P { static void M() { int x = \"text\"; } }", badOptions, badDiagnostics);
        TEST_CHECK(ctx, !broken.IsOk());
        TEST_CHECK(ctx, broken.Status().code == 3);
        TEST_CHECK(ctx, badDiagnostics.HasErrors());
    }
}
