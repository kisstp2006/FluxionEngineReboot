#pragma once

#include "TestFramework.h"

#include <Fluxion/Scene/Scene.h>
#include <Fluxion/Scene/SceneScript.hpp>
#include <Fluxion/Script/Script.hpp>

#include <cstdio>
#include <string>
#include <vector>

// Everything a test needs to have a scene with a script machine behind
// it: the scene, the table describing it to a script, the compiled
// module, and the machine itself, all torn down together.
class ScriptedScene
{
public:
    ScriptedScene() = default;

    ~ScriptedScene()
    {
        if (FLUXION_HANDLE_IS_VALID(m_scene)) Fluxion_Scene_Destroy(m_scene);
        if (m_vm) Fluxion::Script::DestroyVm(m_vm);
    }

    ScriptedScene(const ScriptedScene&) = delete;
    ScriptedScene& operator=(const ScriptedScene&) = delete;

    bool Start(TestContext& ctx, const char* label, const std::string& source)
    {
        using namespace Fluxion::Script;

        m_scene = Fluxion_Scene_Create();
        if (!Fluxion_Scene_IsValid(m_scene))
        {
            std::fprintf(stderr, "  FAIL: '%s' could not create a scene\n", label);
            ++ctx.failures;
            return false;
        }

        DiagnosticList diagnostics;
        if (!Fluxion::Scene::BuildBindingTable(m_scene, m_bindings, diagnostics))
        {
            std::fprintf(stderr, "  FAIL: '%s' could not describe its scene to a script\n", label);
            Report(diagnostics);
            ++ctx.failures;
            return false;
        }

        CompileOptions options;
        options.fileName = label;
        options.bindings = &m_bindings;
        options.hostPrelude = Fluxion::Scene::ComponentPreludeSource();

        auto compiled = Compile(source, options, diagnostics);
        if (!compiled.IsOk())
        {
            std::fprintf(stderr, "  FAIL: '%s' did not compile\n", label);
            Report(diagnostics);
            ++ctx.failures;
            return false;
        }

        m_module = compiled.Value();
        m_vm = CreateVm(m_module, diagnostics, &m_bindings);
        if (!m_vm)
        {
            std::fprintf(stderr, "  FAIL: '%s' did not load\n", label);
            Report(diagnostics);
            ++ctx.failures;
            return false;
        }

        SetOutputHandler(m_vm, &ScriptedScene::Record, this);

        if (!Fluxion::Scene::AttachRuntime(m_scene, m_vm))
        {
            std::fprintf(stderr, "  FAIL: '%s' could not attach its machine: %s\n", label, Fluxion_Scene_GetLastError(m_scene));
            ++ctx.failures;
            return false;
        }
        return true;
    }

    FluxionSceneHandle Scene() const { return m_scene; }
    Fluxion::Script::Vm* Machine() const { return m_vm; }

    // Everything the script has written, one entry per line, in the order
    // it was written. This is what a test asserts a call sequence against.
    const std::vector<std::string>& Lines() const { return m_lines; }
    void ClearLines() { m_lines.clear(); }

    std::string Joined() const
    {
        std::string text;
        for (const std::string& line : m_lines)
        {
            if (!text.empty()) text += ",";
            text += line;
        }
        return text;
    }

    static void Report(const Fluxion::Script::DiagnosticList& diagnostics)
    {
        for (const Fluxion::Script::Diagnostic& entry : diagnostics.entries)
        {
            std::fprintf(stderr, "    %s:%u:%u: %s\n", entry.location.file.c_str(), entry.location.line, entry.location.column,
                entry.message.c_str());
        }
    }

private:
    static void Record(void* user, Fluxion::Script::OutputChannel channel, const char* text)
    {
        (void)channel;
        auto* self = (ScriptedScene*)user;

        std::string line = text ? text : "";
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        if (!line.empty()) self->m_lines.push_back(line);
    }

    FluxionSceneHandle m_scene = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    Fluxion::Script::BindingTable m_bindings;
    Fluxion::Script::CompiledModule m_module;
    Fluxion::Script::Vm* m_vm = nullptr;
    std::vector<std::string> m_lines;
};
