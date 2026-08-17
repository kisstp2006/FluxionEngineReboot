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

#include "SceneFixture.h"

#include <Fluxion/Scene/Scene.h>
#include <Fluxion/Scene/SceneScript.hpp>
#include <Fluxion/Script/Runtime/CompileCache.hpp>

#include <filesystem>
#include <string>
#include <system_error>

using namespace Fluxion::Script;

namespace
{

// --- Two versions of the same component ---------------------------------
//
// The second differs from the first in what its Update does and in having
// gained a method, and in nothing else: the fields are the same fields, so
// everything a reload is meant to carry has somewhere to land.

const char* const kSharedTypes =
    "enum Mood { Calm, Wild }\n";

const char* const kCounterV1 =
    "class Counter : Component\n"
    "{\n"
    "    int ticks;\n"
    "    float rate;\n"
    "    string tag;\n"
    "    Mood mood;\n"
    "    Vector3 offset;\n"
    "    GameObject partner;\n"
    "\n"
    "    void Awake()\n"
    "    {\n"
    "        this.rate = 1.0f;\n"
    "        this.tag = \"woken once\";\n"
    "        this.mood = Mood.Calm;\n"
    "        this.offset = new Vector3(1.0f, 2.0f, 3.0f);\n"
    "        this.partner = this.gameObject;\n"
    "    }\n"
    "\n"
    "    void Update(float deltaTime) { this.ticks = this.ticks + 1; }\n"
    "\n"
    "    int Ticks() { return this.ticks; }\n"
    "    float Rate() { return this.rate; }\n"
    "    string Tag() { return this.tag; }\n"
    "    int MoodNumber() { if (this.mood == Mood.Wild) { return 1; } return 0; }\n"
    "    float OffsetY() { return this.offset.y; }\n"
    "    bool PartnerIsOwner() { return this.partner == this.gameObject; }\n"
    "\n"
    "    void Retag(string what) { this.tag = what; }\n"
    "    void GoWild() { this.mood = Mood.Wild; }\n"
    "}\n";

// Ten a turn instead of one, so a tick after the reload says which code is
// running without anything having to be printed.
const char* const kCounterV2 =
    "class Counter : Component\n"
    "{\n"
    "    int ticks;\n"
    "    float rate;\n"
    "    string tag;\n"
    "    Mood mood;\n"
    "    Vector3 offset;\n"
    "    GameObject partner;\n"
    "\n"
    "    void Awake()\n"
    "    {\n"
    "        this.rate = 99.0f;\n"
    "        this.tag = \"woken again\";\n"
    "        this.mood = Mood.Calm;\n"
    "        this.offset = new Vector3(0.0f, 0.0f, 0.0f);\n"
    "    }\n"
    "\n"
    "    void Update(float deltaTime) { this.ticks = this.ticks + 10; }\n"
    "\n"
    "    int Ticks() { return this.ticks; }\n"
    "    float Rate() { return this.rate; }\n"
    "    string Tag() { return this.tag; }\n"
    "    int MoodNumber() { if (this.mood == Mood.Wild) { return 1; } return 0; }\n"
    "    float OffsetY() { return this.offset.y; }\n"
    "    bool PartnerIsOwner() { return this.partner == this.gameObject; }\n"
    "\n"
    "    void Retag(string what) { this.tag = what; }\n"
    "    void GoWild() { this.mood = Mood.Wild; }\n"
    "    int Doubled() { return this.ticks * 2; }\n"
    "}\n";

// Every index in the module moves, because this is written ahead of
// everything that was there before: indices are handed out in the order
// declarations are added. Anything that kept one across the reload reaches
// the wrong class or the wrong method here.
const char* const kPushedDown =
    "class AaaFirst : Component\n"
    "{\n"
    "    int unused;\n"
    "    void Update(float deltaTime) { this.unused = this.unused + 1; }\n"
    "}\n"
    "class AabSecond\n"
    "{\n"
    "    int a;\n"
    "    int b;\n"
    "    AabSecond() { this.a = 1; this.b = 2; }\n"
    "}\n"
    "class AacThird : Component\n"
    "{\n"
    "    float f;\n"
    "    void Start() { this.f = 1.0f; }\n"
    "}\n";

i32 CallInt(TestContext& ctx, Vm* vm, ObjectHandle instance, u32 classIndex, const char* name)
{
    const u32 method = FindMethod(vm, classIndex, name);
    TEST_CHECK(ctx, method != kNoFunction);
    if (method == kNoFunction) return 0;

    auto result = InvokeMethod(vm, instance, method, nullptr, 0);
    TEST_CHECK(ctx, result.IsOk());
    return result.IsOk() ? result.Value().intValue : 0;
}

f32 CallFloat(TestContext& ctx, Vm* vm, ObjectHandle instance, u32 classIndex, const char* name)
{
    const u32 method = FindMethod(vm, classIndex, name);
    TEST_CHECK(ctx, method != kNoFunction);
    if (method == kNoFunction) return 0.0f;

    auto result = InvokeMethod(vm, instance, method, nullptr, 0);
    TEST_CHECK(ctx, result.IsOk());
    return result.IsOk() ? result.Value().floatValue : 0.0f;
}

std::string CallString(TestContext& ctx, Vm* vm, ObjectHandle instance, u32 classIndex, const char* name)
{
    const u32 method = FindMethod(vm, classIndex, name);
    TEST_CHECK(ctx, method != kNoFunction);
    if (method == kNoFunction) return std::string();

    auto result = InvokeMethod(vm, instance, method, nullptr, 0);
    TEST_CHECK(ctx, result.IsOk());
    return result.IsOk() ? result.Value().stringValue : std::string();
}

bool CallBool(TestContext& ctx, Vm* vm, ObjectHandle instance, u32 classIndex, const char* name)
{
    const u32 method = FindMethod(vm, classIndex, name);
    TEST_CHECK(ctx, method != kNoFunction);
    if (method == kNoFunction) return false;

    auto result = InvokeMethod(vm, instance, method, nullptr, 0);
    TEST_CHECK(ctx, result.IsOk());
    return result.IsOk() && result.Value().boolValue;
}

void CallWithText(TestContext& ctx, Vm* vm, ObjectHandle instance, u32 classIndex, const char* name, const char* text)
{
    const u32 method = FindMethod(vm, classIndex, name);
    TEST_CHECK(ctx, method != kNoFunction);
    if (method == kNoFunction) return;

    ScriptValue argument;
    argument.type = ValueType::String;
    argument.stringValue = text;

    auto result = InvokeMethod(vm, instance, method, &argument, 1);
    TEST_CHECK(ctx, result.IsOk());
}

void CallVoid(TestContext& ctx, Vm* vm, ObjectHandle instance, u32 classIndex, const char* name)
{
    const u32 method = FindMethod(vm, classIndex, name);
    TEST_CHECK(ctx, method != kNoFunction);
    if (method == kNoFunction) return;

    auto result = InvokeMethod(vm, instance, method, nullptr, 0);
    TEST_CHECK(ctx, result.IsOk());
}

// Every cache directory this run claimed, so they can be taken away
// again at the end.
std::vector<std::filesystem::path> g_cacheDirectories;

// A directory nobody else is writing into.
//
// The checks below count how many modules were COMPILED and how many
// came off disk, which only means anything if this cache is this run's
// alone. A fixed path is not: a second copy of this binary would clear
// the directory and warm it in the middle of the first one's counting,
// and both would then see numbers that belong to neither.
//
// Claimed by creating rather than by naming: create_directories says
// false when the directory is already there, so the first name that
// succeeds is one no other process holds. No process id is needed, and
// none is portable anyway.
std::filesystem::path MakeCacheDirectory(const char* name)
{
    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path(error);
    if (error) return std::filesystem::path();

    const std::filesystem::path parent = root / "FluxionSceneReloadTests";
    for (u32 attempt = 0; attempt < 4096; ++attempt)
    {
        const std::filesystem::path directory = parent / (std::string(name) + "-" + std::to_string(attempt));

        error.clear();
        if (std::filesystem::create_directories(directory, error) && !error)
        {
            g_cacheDirectories.push_back(directory);
            return directory;
        }
    }

    return std::filesystem::path();
}

void RemoveCacheDirectories()
{
    std::error_code error;
    for (const std::filesystem::path& directory : g_cacheDirectories) std::filesystem::remove_all(directory, error);
    g_cacheDirectories.clear();
}

// Two shapes of the same component. The second declares something ahead
// of it, so every index in the module moves between one reload and the
// next -- and the alternation means that happens on every single one,
// not just the first.
const char* const kTickerPlain =
    "class Ticker : Component\n"
    "{\n"
    "    int ticks;\n"
    "    void Update(float deltaTime) { this.ticks = this.ticks + 1; }\n"
    "    int Ticks() { return this.ticks; }\n"
    "    int Live() { Gc.Collect(); return Gc.LiveObjects(); }\n"
    "}\n";

const char* const kTickerShifted =
    "class Ahead { int unused; }\n"
    "class Ticker : Component\n"
    "{\n"
    "    int ticks;\n"
    "    void Update(float deltaTime) { this.ticks = this.ticks + 1; }\n"
    "    int Ticks() { return this.ticks; }\n"
    "    int Live() { Gc.Collect(); return Gc.LiveObjects(); }\n"
    "}\n";

} // namespace


void Test_Reload_Run(TestContext& ctx)
{
    {
        // Reloading is not a thing done once. Every reload builds a new
        // machine, pins new instances into it and lets the old ones go,
        // and if any part of that only half happens -- a record kept
        // beside its replacement, an instance pinned and never released --
        // nothing fails and nothing is reported. It shows up only as a
        // count that keeps climbing, which is why the count is what is
        // checked here rather than any single reload's outcome.
        ScriptedScene scene;
        if (scene.Start(ctx, "reload-does-not-accumulate.fls", kTickerPlain))
        {
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene.Scene(), "ticker");
            const u32 first = Fluxion::Scene::FindComponentClass(scene.Scene(), "Ticker");
            TEST_CHECK(ctx, first != kNoClass);
            TEST_CHECK(ctx, !Fluxion::Scene::AddComponent(scene.Scene(), object, first).IsNull());

            Fluxion_Scene_Tick(scene.Scene(), 0.016f);

            i32 settled = 0;
            bool held = true;
            for (int round = 0; round < 24 && held; ++round)
            {
                DiagnosticList diagnostics;
                Fluxion::Scene::ReloadReport report;
                const bool reloaded = scene.Reload((round % 2) == 0 ? kTickerShifted : kTickerPlain, diagnostics, report);
                if (!reloaded)
                {
                    ScriptedScene::Report(diagnostics);
                    held = false;
                    break;
                }

                const u32 klass = Fluxion::Scene::FindComponentClass(scene.Scene(), "Ticker");
                if (klass == kNoClass) { held = false; break; }

                // One component, still, and only one.
                if (Fluxion::Scene::ComponentCount(scene.Scene(), object) != 1) { held = false; break; }

                ObjectHandle instance = Fluxion::Scene::GetComponent(scene.Scene(), object, klass);
                if (instance.IsNull()) { held = false; break; }

                Fluxion_Scene_Tick(scene.Scene(), 0.016f);

                // What it counted before the reload is still counted, and
                // the new code keeps counting from there.
                if (CallInt(ctx, scene.Machine(), instance, klass, "Ticks") != round + 2) { held = false; break; }

                const i32 live = CallInt(ctx, scene.Machine(), instance, klass, "Live");

                // The second round is the reference: by then the machine
                // is doing what it will do every time. Comparing against
                // the first would measure start-up instead.
                if (round == 1) settled = live;
                if (round > 1 && live != settled) { held = false; break; }
            }
            TEST_CHECK(ctx, held);
            TEST_CHECK(ctx, settled > 0);
        }
    }
    {
        // What a component holds survives, and what it does is the new
        // code's business afterwards.
        ScriptedScene scene;
        if (scene.Start(ctx, "reload-carries-state.fls", std::string(kSharedTypes) + kCounterV1))
        {
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene.Scene(), "counter");
            const u32 before = Fluxion::Scene::FindComponentClass(scene.Scene(), "Counter");
            TEST_CHECK(ctx, before != kNoClass);
            TEST_CHECK(ctx, !Fluxion::Scene::AddComponent(scene.Scene(), object, before).IsNull());

            for (int i = 0; i < 3; ++i) Fluxion_Scene_Tick(scene.Scene(), 0.016f);

            ObjectHandle instance = Fluxion::Scene::GetComponent(scene.Scene(), object, before);
            TEST_CHECK(ctx, CallInt(ctx, scene.Machine(), instance, before, "Ticks") == 3);

            // Set after Awake ran, so a reload that ran Awake again would
            // put the other text back and be caught here.
            CallWithText(ctx, scene.Machine(), instance, before, "Retag", "set by hand");
            CallVoid(ctx, scene.Machine(), instance, before, "GoWild");

            DiagnosticList diagnostics;
            Fluxion::Scene::ReloadReport report;
            const bool reloaded = scene.Reload(std::string(kSharedTypes) + kCounterV2, diagnostics, report);
            TEST_CHECK(ctx, reloaded);
            if (!reloaded) ScriptedScene::Report(diagnostics);

            if (reloaded)
            {
                TEST_CHECK(ctx, report.reloaded);
                TEST_CHECK(ctx, report.componentsCarried == 1);

                const u32 after = Fluxion::Scene::FindComponentClass(scene.Scene(), "Counter");
                TEST_CHECK(ctx, after != kNoClass);

                instance = Fluxion::Scene::GetComponent(scene.Scene(), object, after);
                TEST_CHECK(ctx, !instance.IsNull());

                // Everything the old instance held, in the new one.
                TEST_CHECK(ctx, CallInt(ctx, scene.Machine(), instance, after, "Ticks") == 3);
                TEST_CHECK(ctx, CallFloat(ctx, scene.Machine(), instance, after, "Rate") == 1.0f);
                TEST_CHECK(ctx, CallString(ctx, scene.Machine(), instance, after, "Tag") == "set by hand");
                TEST_CHECK(ctx, CallInt(ctx, scene.Machine(), instance, after, "MoodNumber") == 1);
                TEST_CHECK(ctx, CallFloat(ctx, scene.Machine(), instance, after, "OffsetY") == 2.0f);
                TEST_CHECK(ctx, CallBool(ctx, scene.Machine(), instance, after, "PartnerIsOwner"));

                // And the new code is what runs from here on: ten a turn
                // rather than one, without Awake or Start being run again.
                Fluxion_Scene_Tick(scene.Scene(), 0.016f);
                TEST_CHECK(ctx, CallInt(ctx, scene.Machine(), instance, after, "Ticks") == 13);
                TEST_CHECK(ctx, CallInt(ctx, scene.Machine(), instance, after, "Doubled") == 26);
                TEST_CHECK(ctx, CallString(ctx, scene.Machine(), instance, after, "Tag") == "set by hand");
            }
        }
    }

    {
        // Source that does not compile changes nothing at all: the scene
        // goes on running what it was running, and the reason says where
        // in the source it was found.
        ScriptedScene scene;
        if (scene.Start(ctx, "reload-refuses-broken.fls", std::string(kSharedTypes) + kCounterV1))
        {
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene.Scene(), "counter");
            const u32 counter = Fluxion::Scene::FindComponentClass(scene.Scene(), "Counter");
            Fluxion::Scene::AddComponent(scene.Scene(), object, counter);
            Fluxion_Scene_Tick(scene.Scene(), 0.016f);

            Vm* const machineBefore = scene.Machine();
            const ObjectHandle instanceBefore = Fluxion::Scene::GetComponent(scene.Scene(), object, counter);

            DiagnosticList diagnostics;
            Fluxion::Scene::ReloadReport report;
            const bool reloaded = scene.Reload(
                std::string(kSharedTypes) +
                    "class Counter : Component\n"
                    "{\n"
                    "    int ticks;\n"
                    "    void Update(float deltaTime) { this.ticks = this.ticks + }\n"
                    "}\n",
                diagnostics, report);

            TEST_CHECK(ctx, !reloaded);
            TEST_CHECK(ctx, !report.reloaded);
            TEST_CHECK(ctx, report.retired == nullptr);
            TEST_CHECK(ctx, diagnostics.HasErrors());

            bool locatedInSource = false;
            for (const Diagnostic& entry : diagnostics.entries)
            {
                if (entry.severity != DiagnosticSeverity::Error) continue;
                if (entry.location.file != "reload-refuses-broken.fls") continue;
                if (entry.location.line == 0) continue;
                locatedInSource = true;
            }
            TEST_CHECK(ctx, locatedInSource);

            // The same machine, the same instance, still being driven.
            TEST_CHECK(ctx, scene.Machine() == machineBefore);
            TEST_CHECK(ctx, Fluxion::Scene::GetComponent(scene.Scene(), object, counter) == instanceBefore);
            TEST_CHECK(ctx, CallInt(ctx, scene.Machine(), instanceBefore, counter, "Ticks") == 1);

            Fluxion_Scene_Tick(scene.Scene(), 0.016f);
            TEST_CHECK(ctx, CallInt(ctx, scene.Machine(), instanceBefore, counter, "Ticks") == 2);
        }
    }

    {
        // Three declarations written ahead of everything that was already
        // there, so every class index and every method index the scene had
        // is now somebody else's. Nothing may be reached by a number that
        // was worked out before.
        ScriptedScene scene;
        if (scene.Start(ctx, "reload-shifts-every-index.fls", std::string(kSharedTypes) + kCounterV1))
        {
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene.Scene(), "counter");
            const u32 before = Fluxion::Scene::FindComponentClass(scene.Scene(), "Counter");
            Fluxion::Scene::AddComponent(scene.Scene(), object, before);

            for (int i = 0; i < 5; ++i) Fluxion_Scene_Tick(scene.Scene(), 0.016f);
            TEST_CHECK(ctx, CallInt(ctx, scene.Machine(), Fluxion::Scene::GetComponent(scene.Scene(), object, before), before,
                            "Ticks") == 5);

            DiagnosticList diagnostics;
            Fluxion::Scene::ReloadReport report;
            const bool reloaded = scene.Reload(std::string(kSharedTypes) + kPushedDown + kCounterV2, diagnostics, report);
            TEST_CHECK(ctx, reloaded);
            if (!reloaded) ScriptedScene::Report(diagnostics);

            if (reloaded)
            {
                const u32 after = Fluxion::Scene::FindComponentClass(scene.Scene(), "Counter");
                TEST_CHECK(ctx, after != kNoClass);
                TEST_CHECK(ctx, after != before);

                const ObjectHandle instance = Fluxion::Scene::GetComponent(scene.Scene(), object, after);
                TEST_CHECK(ctx, !instance.IsNull());
                TEST_CHECK(ctx, ObjectClass(scene.Machine(), instance) == after);
                TEST_CHECK(ctx, CallInt(ctx, scene.Machine(), instance, after, "Ticks") == 5);
                TEST_CHECK(ctx, CallString(ctx, scene.Machine(), instance, after, "Tag") == "woken once");

                // The lifecycle methods were looked up again too: a stale
                // one would either fault or run somebody else's body.
                Fluxion_Scene_Tick(scene.Scene(), 0.016f);
                TEST_CHECK(ctx, CallInt(ctx, scene.Machine(), instance, after, "Ticks") == 15);

                // And a component of one of the newly written classes can
                // still be attached afterwards.
                const u32 first = Fluxion::Scene::FindComponentClass(scene.Scene(), "AaaFirst");
                TEST_CHECK(ctx, first != kNoClass);
                TEST_CHECK(ctx, !Fluxion::Scene::AddComponent(scene.Scene(), object, first).IsNull());
                Fluxion_Scene_Tick(scene.Scene(), 0.016f);
                TEST_CHECK(ctx, CallInt(ctx, scene.Machine(), instance, after, "Ticks") == 25);
            }
        }
    }

    {
        // The machine standing down is left holding nothing: a collection
        // on it reclaims every component the scene had, which it could not
        // do if the scene had not let go of them. Without that, every
        // reload would leave a heap that never shrinks.
        ScriptedScene scene;
        if (scene.Start(ctx, "reload-lets-go.fls", std::string(kSharedTypes) + kCounterV1))
        {
            FluxionGameObjectHandle first = Fluxion_Scene_CreateGameObject(scene.Scene(), "one");
            FluxionGameObjectHandle second = Fluxion_Scene_CreateGameObject(scene.Scene(), "two");

            const u32 counter = Fluxion::Scene::FindComponentClass(scene.Scene(), "Counter");
            const ObjectHandle a = Fluxion::Scene::AddComponent(scene.Scene(), first, counter);
            const ObjectHandle b = Fluxion::Scene::AddComponent(scene.Scene(), second, counter);
            TEST_CHECK(ctx, !a.IsNull() && !b.IsNull());
            Fluxion_Scene_Tick(scene.Scene(), 0.016f);

            Vm* const oldMachine = scene.Machine();
            CollectGarbage(oldMachine);
            const HeapStats held = GetHeapStats(oldMachine);
            TEST_CHECK(ctx, held.liveObjects >= 2);
            TEST_CHECK(ctx, IsObjectAlive(oldMachine, a) && IsObjectAlive(oldMachine, b));

            DiagnosticList diagnostics;
            Fluxion::Scene::ReloadReport report;
            const bool reloaded = scene.Reload(std::string(kSharedTypes) + kCounterV2, diagnostics, report, nullptr, true);
            TEST_CHECK(ctx, reloaded);
            if (!reloaded) ScriptedScene::Report(diagnostics);

            if (reloaded)
            {
                TEST_CHECK(ctx, report.retired == oldMachine);
                TEST_CHECK(ctx, report.componentsCarried == 2);

                // Nothing holds the old instances up any more, so a
                // collection takes them.
                CollectGarbage(oldMachine);
                TEST_CHECK(ctx, !IsObjectAlive(oldMachine, a));
                TEST_CHECK(ctx, !IsObjectAlive(oldMachine, b));

                const HeapStats emptied = GetHeapStats(oldMachine);
                TEST_CHECK(ctx, emptied.liveObjects == 0);
                TEST_CHECK(ctx, emptied.liveObjects < held.liveObjects);

                DestroyVm(report.retired);
                report.retired = nullptr;

                // The machine taking over holds exactly the two it made,
                // and holds them properly: a collection there takes
                // nothing.
                CollectGarbage(scene.Machine());
                const u32 carriedClass = Fluxion::Scene::FindComponentClass(scene.Scene(), "Counter");
                const ObjectHandle newA = Fluxion::Scene::GetComponent(scene.Scene(), first, carriedClass);
                const ObjectHandle newB = Fluxion::Scene::GetComponent(scene.Scene(), second, carriedClass);
                TEST_CHECK(ctx, IsObjectAlive(scene.Machine(), newA));
                TEST_CHECK(ctx, IsObjectAlive(scene.Machine(), newB));

                Fluxion_Scene_Tick(scene.Scene(), 0.016f);
                TEST_CHECK(ctx, CallInt(ctx, scene.Machine(), newA, carriedClass, "Ticks") == 11);
            }
        }
    }

    {
        // A field that is gone, one that is declared with another type now,
        // and one that is new. Each is dealt with on its own: nothing is
        // forced into a slot that no longer means what it meant.
        ScriptedScene scene;
        if (scene.Start(ctx, "reload-fields-that-moved.fls",
                "class Holder : Component\n"
                "{\n"
                "    int kept;\n"
                "    int dropped;\n"
                "    int changed;\n"
                "    void Awake() { this.kept = 7; this.dropped = 8; this.changed = 9; }\n"
                "    int Kept() { return this.kept; }\n"
                "}\n"))
        {
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene.Scene(), "holder");
            const u32 before = Fluxion::Scene::FindComponentClass(scene.Scene(), "Holder");
            Fluxion::Scene::AddComponent(scene.Scene(), object, before);
            Fluxion_Scene_Tick(scene.Scene(), 0.016f);

            DiagnosticList diagnostics;
            Fluxion::Scene::ReloadReport report;
            const bool reloaded = scene.Reload(
                "class Holder : Component\n"
                "{\n"
                "    int kept;\n"
                "    float changed;\n"
                "    string added;\n"
                "    void Awake() { this.kept = 0; }\n"
                "    int Kept() { return this.kept; }\n"
                "    float Changed() { return this.changed; }\n"
                "    string Added() { return this.added; }\n"
                "}\n",
                diagnostics, report);

            TEST_CHECK(ctx, reloaded);
            if (!reloaded) ScriptedScene::Report(diagnostics);

            if (reloaded)
            {
                TEST_CHECK(ctx, report.fieldsCarried == 1);
                TEST_CHECK(ctx, report.fieldsDropped == 2);

                const u32 after = Fluxion::Scene::FindComponentClass(scene.Scene(), "Holder");
                const ObjectHandle instance = Fluxion::Scene::GetComponent(scene.Scene(), object, after);

                TEST_CHECK(ctx, CallInt(ctx, scene.Machine(), instance, after, "Kept") == 7);

                // Declared with another type now, so it starts at what the
                // new class starts it at rather than at the old nine.
                TEST_CHECK(ctx, CallFloat(ctx, scene.Machine(), instance, after, "Changed") == 0.0f);
                TEST_CHECK(ctx, CallString(ctx, scene.Machine(), instance, after, "Added").empty());
            }
        }
    }

    {
        // A reference field is not carried: what it names belongs to the
        // machine standing down. It is counted as left behind and the new
        // instance starts with nothing there.
        ScriptedScene scene;
        if (scene.Start(ctx, "reload-drops-references.fls",
                "class Note { int value; Note(int v) { this.value = v; } int Value() { return this.value; } }\n"
                "class Keeper : Component\n"
                "{\n"
                "    int number;\n"
                "    Note note;\n"
                "    void Awake() { this.number = 4; this.note = new Note(41); }\n"
                "    int Number() { return this.number; }\n"
                "    bool HasNote() { return this.note != null; }\n"
                "}\n"))
        {
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene.Scene(), "keeper");
            const u32 before = Fluxion::Scene::FindComponentClass(scene.Scene(), "Keeper");
            Fluxion::Scene::AddComponent(scene.Scene(), object, before);
            Fluxion_Scene_Tick(scene.Scene(), 0.016f);

            const ObjectHandle instanceBefore = Fluxion::Scene::GetComponent(scene.Scene(), object, before);
            TEST_CHECK(ctx, CallBool(ctx, scene.Machine(), instanceBefore, before, "HasNote"));

            DiagnosticList diagnostics;
            Fluxion::Scene::ReloadReport report;
            const bool reloaded = scene.Reload(
                "class Note { int value; Note(int v) { this.value = v; } int Value() { return this.value; } }\n"
                "class Keeper : Component\n"
                "{\n"
                "    int number;\n"
                "    Note note;\n"
                "    void Awake() { this.number = 0; }\n"
                "    int Number() { return this.number; }\n"
                "    bool HasNote() { return this.note != null; }\n"
                "}\n",
                diagnostics, report);

            TEST_CHECK(ctx, reloaded);
            if (reloaded)
            {
                TEST_CHECK(ctx, report.fieldsCarried == 1);
                TEST_CHECK(ctx, report.fieldsDropped == 1);

                const u32 after = Fluxion::Scene::FindComponentClass(scene.Scene(), "Keeper");
                const ObjectHandle instance = Fluxion::Scene::GetComponent(scene.Scene(), object, after);
                TEST_CHECK(ctx, CallInt(ctx, scene.Machine(), instance, after, "Number") == 4);
                TEST_CHECK(ctx, !CallBool(ctx, scene.Machine(), instance, after, "HasNote"));
            }
        }
    }

    {
        // Source the scene's own components are not in is refused before
        // anything is disturbed: attaching them again would need the class
        // the new code no longer has.
        ScriptedScene scene;
        if (scene.Start(ctx, "reload-refuses-missing-class.fls", std::string(kSharedTypes) + kCounterV1))
        {
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene.Scene(), "counter");
            const u32 counter = Fluxion::Scene::FindComponentClass(scene.Scene(), "Counter");
            Fluxion::Scene::AddComponent(scene.Scene(), object, counter);
            Fluxion_Scene_Tick(scene.Scene(), 0.016f);

            Vm* const machineBefore = scene.Machine();

            DiagnosticList diagnostics;
            Fluxion::Scene::ReloadReport report;
            const bool reloaded = scene.Reload(
                "class Different : Component { int n; void Update(float deltaTime) { this.n = this.n + 1; } }\n",
                diagnostics, report);

            TEST_CHECK(ctx, !reloaded);
            TEST_CHECK(ctx, report.retired == nullptr);
            TEST_CHECK(ctx, scene.Machine() == machineBefore);

            const ObjectHandle instance = Fluxion::Scene::GetComponent(scene.Scene(), object, counter);
            Fluxion_Scene_Tick(scene.Scene(), 0.016f);
            TEST_CHECK(ctx, CallInt(ctx, scene.Machine(), instance, counter, "Ticks") == 2);
        }
    }

    {
        // A lifecycle method written with a shape the scene cannot call is
        // caught at the same point, before anything moves.
        ScriptedScene scene;
        if (scene.Start(ctx, "reload-refuses-wrong-shape.fls", std::string(kSharedTypes) + kCounterV1))
        {
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene.Scene(), "counter");
            const u32 counter = Fluxion::Scene::FindComponentClass(scene.Scene(), "Counter");
            Fluxion::Scene::AddComponent(scene.Scene(), object, counter);

            Vm* const machineBefore = scene.Machine();

            DiagnosticList diagnostics;
            Fluxion::Scene::ReloadReport report;
            const bool reloaded = scene.Reload(
                std::string(kSharedTypes) +
                    "class Counter : Component\n"
                    "{\n"
                    "    int ticks;\n"
                    "    void Update(int wrong) { this.ticks = this.ticks + wrong; }\n"
                    "}\n",
                diagnostics, report);

            TEST_CHECK(ctx, !reloaded);
            TEST_CHECK(ctx, scene.Machine() == machineBefore);
        }
    }

    {
        // A reload can be answered out of what an earlier one compiled --
        // which also puts a module that calls into the engine through the
        // writer and the reader, since a component reaches its object that
        // way.
        const std::filesystem::path directory = MakeCacheDirectory("reload");
        TEST_CHECK(ctx, !directory.empty());

        ScriptedScene scene;
        if (!directory.empty() && scene.Start(ctx, "reload-through-the-cache.fls", std::string(kSharedTypes) + kCounterV1))
        {
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene.Scene(), "counter");
            const u32 counter = Fluxion::Scene::FindComponentClass(scene.Scene(), "Counter");
            Fluxion::Scene::AddComponent(scene.Scene(), object, counter);
            Fluxion_Scene_Tick(scene.Scene(), 0.016f);

            const std::string second = std::string(kSharedTypes) + kCounterV2;
            const std::string third = std::string(kSharedTypes) + kPushedDown + kCounterV2;

            DiagnosticList diagnostics;
            Fluxion::Scene::ReloadReport report;

            CompileCacheCounters before = GetCompileCacheCounters();
            TEST_CHECK(ctx, scene.Reload(second, diagnostics, report, directory.string().c_str()));
            CompileCacheCounters after = GetCompileCacheCounters();
            TEST_CHECK(ctx, after.compiled - before.compiled == 1);
            TEST_CHECK(ctx, after.loaded - before.loaded == 0);

            // Away and back again. The way back is a compilation an
            // earlier one already produced, so it comes off disk.
            TEST_CHECK(ctx, scene.Reload(third, diagnostics, report, directory.string().c_str()));

            before = GetCompileCacheCounters();
            TEST_CHECK(ctx, scene.Reload(second, diagnostics, report, directory.string().c_str()));
            after = GetCompileCacheCounters();
            TEST_CHECK(ctx, after.compiled - before.compiled == 0);
            TEST_CHECK(ctx, after.loaded - before.loaded == 1);

            // And what came off disk runs, drives the scene, and still has
            // everything the first version was holding.
            const u32 reloadedClass = Fluxion::Scene::FindComponentClass(scene.Scene(), "Counter");
            const ObjectHandle instance = Fluxion::Scene::GetComponent(scene.Scene(), object, reloadedClass);
            TEST_CHECK(ctx, !instance.IsNull());
            TEST_CHECK(ctx, CallString(ctx, scene.Machine(), instance, reloadedClass, "Tag") == "woken once");
            TEST_CHECK(ctx, CallBool(ctx, scene.Machine(), instance, reloadedClass, "PartnerIsOwner"));

            const i32 ticksBefore = CallInt(ctx, scene.Machine(), instance, reloadedClass, "Ticks");
            Fluxion_Scene_Tick(scene.Scene(), 0.016f);
            TEST_CHECK(ctx, CallInt(ctx, scene.Machine(), instance, reloadedClass, "Ticks") == ticksBefore + 10);
        }
    }

    // The directories are claimed by creating them, so leaving them
    // behind would make every later run climb past a longer list before
    // finding a free name.
    RemoveCacheDirectories();
}
