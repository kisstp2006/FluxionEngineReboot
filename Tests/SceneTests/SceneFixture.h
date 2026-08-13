#pragma once

#include "TestFramework.h"

#include <Fluxion/Scene/EngineScript.hpp>
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

    // `withEngineTypes` also makes the clock, the input state and the
    // drawing surface visible, and compiles the sets of constants that go
    // with them. Left out by default: a test about a scene should fail
    // for reasons about that scene, not because something the rest of the
    // engine offers could not be described.
    bool Start(TestContext& ctx, const char* label, const std::string& source, bool withEngineTypes = false)
    {
        using namespace Fluxion::Script;

        m_label = label;
        m_withEngineTypes = withEngineTypes;
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

        if (withEngineTypes && !Fluxion::Scene::BuildEngineBindings(m_scene, m_bindings, diagnostics))
        {
            std::fprintf(stderr, "  FAIL: '%s' could not describe the rest of the engine to a script\n", label);
            Report(diagnostics);
            ++ctx.failures;
            return false;
        }

        CompileOptions options;
        options.fileName = label;
        options.bindings = &m_bindings;
        options.hostPrelude = Fluxion::Scene::ComponentPreludeSource();
        if (withEngineTypes) options.hostPrelude += Fluxion::Scene::EnginePreludeSource();

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

    // Puts new source under the running scene. False means it was refused
    // and the scene is still running exactly what it was; `outDiagnostics`
    // says why, with the file and line for anything the compiler found.
    //
    // The machine that was stood down is released here unless the caller
    // asks to keep it, which is what a test looking at what that machine
    // is still holding needs -- it then owns it and destroys it itself.
    bool Reload(const std::string& source, Fluxion::Script::DiagnosticList& outDiagnostics,
        Fluxion::Scene::ReloadReport& outReport, const char* cacheDirectory = nullptr, bool keepRetired = false)
    {
        Fluxion::Scene::ReloadRequest request;
        request.source = source;
        request.options.fileName = m_label;
        request.options.bindings = &m_bindings;
        request.options.hostPrelude = Fluxion::Scene::ComponentPreludeSource();
        if (m_withEngineTypes) request.options.hostPrelude += Fluxion::Scene::EnginePreludeSource();
        if (cacheDirectory != nullptr) request.cache.directory = cacheDirectory;

        const bool reloaded = Fluxion::Scene::ReloadRuntime(m_scene, request, outDiagnostics, outReport);
        if (!reloaded) return false;

        m_vm = Fluxion::Scene::GetRuntime(m_scene);
        if (!keepRetired)
        {
            Fluxion::Script::DestroyVm(outReport.retired);
            outReport.retired = nullptr;
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

    const char* m_label = "<source>";
    bool m_withEngineTypes = false;
    FluxionSceneHandle m_scene = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    Fluxion::Script::BindingTable m_bindings;
    Fluxion::Script::CompiledModule m_module;
    Fluxion::Script::Vm* m_vm = nullptr;
    std::vector<std::string> m_lines;
};
