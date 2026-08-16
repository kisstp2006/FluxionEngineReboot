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

using namespace Fluxion::Script;

namespace
{

// Two components that write down every lifecycle method they are given,
// so what a test asserts against is the sequence of calls rather than a
// count of them.
const char* const kRecorderSource =
    "class Alpha : Component\n"
    "{\n"
    "    float lastDelta;\n"
    "    int ticks;\n"
    "\n"
    "    void Awake() { Console.WriteLine(\"Alpha:Awake\"); }\n"
    "    void Start() { Console.WriteLine(\"Alpha:Start\"); }\n"
    "    void Update(float deltaTime)\n"
    "    {\n"
    "        this.lastDelta = deltaTime;\n"
    "        this.ticks = this.ticks + 1;\n"
    "        Console.WriteLine(\"Alpha:Update\");\n"
    "    }\n"
    "    void LateUpdate(float deltaTime) { Console.WriteLine(\"Alpha:LateUpdate\"); }\n"
    "    void OnDestroy() { Console.WriteLine(\"Alpha:OnDestroy\"); }\n"
    "\n"
    "    float LastDelta() { return this.lastDelta; }\n"
    "    int Ticks() { return this.ticks; }\n"
    "}\n"
    "class Beta : Component\n"
    "{\n"
    "    void Awake() { Console.WriteLine(\"Beta:Awake\"); }\n"
    "    void Start() { Console.WriteLine(\"Beta:Start\"); }\n"
    "    void Update(float deltaTime) { Console.WriteLine(\"Beta:Update\"); }\n"
    "    void LateUpdate(float deltaTime) { Console.WriteLine(\"Beta:LateUpdate\"); }\n"
    "    void OnDestroy() { Console.WriteLine(\"Beta:OnDestroy\"); }\n"
    "}\n"
    "class Quiet : Component\n"
    "{\n"
    "    int seen;\n"
    "    void Update(float deltaTime) { this.seen = this.seen + 1; }\n"
    "    int Seen() { return this.seen; }\n"
    "}\n";

f32 InvokeFloat(TestContext& ctx, Vm* vm, ObjectHandle instance, u32 classIndex, const char* name)
{
    const u32 method = FindMethod(vm, classIndex, name);
    TEST_CHECK(ctx, method != kNoFunction);
    if (method == kNoFunction) return 0.0f;

    auto result = InvokeMethod(vm, instance, method, nullptr, 0);
    TEST_CHECK(ctx, result.IsOk());
    return result.IsOk() ? result.Value().floatValue : 0.0f;
}

i32 InvokeInt(TestContext& ctx, Vm* vm, ObjectHandle instance, u32 classIndex, const char* name)
{
    const u32 method = FindMethod(vm, classIndex, name);
    TEST_CHECK(ctx, method != kNoFunction);
    if (method == kNoFunction) return 0;

    auto result = InvokeMethod(vm, instance, method, nullptr, 0);
    TEST_CHECK(ctx, result.IsOk());
    return result.IsOk() ? result.Value().intValue : 0;
}

} // namespace

void Test_Components_Run(TestContext& ctx)
{
    {
        // Attaching from inside a running component suspends the caller's
        // frames. If that suspension also blocked collection permanently,
        // a component that allocates every tick would grow the heap
        // without bound -- so run many ticks and check it does not.
        ScriptedScene scene;
        if (scene.Start(ctx, "allocating-across-reentrant-attach",
                "class Filler { int a; int b; Filler(int v) { this.a = v; this.b = v; } }\n"
                "class Spawner : Component\n"
                "{\n"
                "    int ticks;\n"
                "    void Update(float deltaTime)\n"
                "    {\n"
                "        for (int i = 0; i < 200; i += 1) { Filler junk = new Filler(i); }\n"
                "        this.ticks += 1;\n"
                "    }\n"
                "}\n"
                "static class Probe { static int Live() { Gc.Collect(); return Gc.LiveObjects(); } }\n"))
        {
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene.Scene(), "spawner");
            TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(object));

            const u32 spawnerClass = Fluxion::Scene::FindComponentClass(scene.Scene(), "Spawner");
            TEST_CHECK(ctx, spawnerClass != kNoClass);
            Fluxion::Scene::AddComponent(scene.Scene(), object, spawnerClass);

            for (int tick = 0; tick < 40; ++tick) Fluxion_Scene_Tick(scene.Scene(), 0.016f);

            // 40 ticks x 200 short-lived objects = 8000 allocations. If
            // collection never ran, essentially all of them would still be
            // counted as live here.
            auto live = Fluxion::Script::Invoke(scene.Machine(), "Probe.Live");
            TEST_CHECK(ctx, live.IsOk());
            if (live.IsOk()) TEST_CHECK(ctx, live.Value().intValue < 500);

            const Fluxion::Script::HeapStats stats = Fluxion::Script::GetHeapStats(scene.Machine());
            TEST_CHECK(ctx, stats.totalAllocations >= 8000);
        }
    }
    {
        // A component that keeps running after a collection must still be
        // the same object with the same field values -- the scene is the
        // only thing holding it, so this is what pinning buys.
        ScriptedScene scene;
        if (scene.Start(ctx, "component-state-survives-collection",
                "class Filler { int a; Filler(int v) { this.a = v; } }\n"
                "class Counter : Component\n"
                "{\n"
                "    int seen;\n"
                "    void Update(float deltaTime)\n"
                "    {\n"
                "        for (int i = 0; i < 150; i += 1) { Filler junk = new Filler(i); }\n"
                "        this.seen += 2;\n"
                "        Console.WriteLine(this.seen);\n"
                "    }\n"
                "}\n"))
        {
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene.Scene(), "counter");
            const u32 counterClass = Fluxion::Scene::FindComponentClass(scene.Scene(), "Counter");
            Fluxion::Scene::AddComponent(scene.Scene(), object, counterClass);

            for (int tick = 0; tick < 5; ++tick) Fluxion_Scene_Tick(scene.Scene(), 0.016f);

            // The field accumulated across every tick, so the instance was
            // never replaced or reclaimed underneath the scene.
            TEST_CHECK(ctx, scene.Joined() == "2,4,6,8,10");
        }
    }
    // --- What happens in one turn, and in what order --------------------

    {
        ScriptedScene run;
        if (run.Start(ctx, "lifecycle-order", kRecorderSource))
        {
            const FluxionSceneHandle scene = run.Scene();
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "object");

            const u32 alpha = Fluxion::Scene::FindComponentClass(scene, "Alpha");
            const u32 beta = Fluxion::Scene::FindComponentClass(scene, "Beta");
            TEST_CHECK(ctx, alpha != kNoClass && beta != kNoClass);

            const ObjectHandle alphaInstance = Fluxion::Scene::AddComponent(scene, object, alpha);
            const ObjectHandle betaInstance = Fluxion::Scene::AddComponent(scene, object, beta);
            TEST_CHECK(ctx, !alphaInstance.IsNull());
            TEST_CHECK(ctx, !betaInstance.IsNull());
            TEST_CHECK(ctx, Fluxion::Scene::ComponentCount(scene, object) == 2);

            // Attaching alone runs nothing: everything happens in a turn.
            TEST_CHECK(ctx, run.Lines().empty());

            Fluxion_Scene_Tick(scene, 0.25f);
            TEST_CHECK(ctx, run.Joined() ==
                                "Alpha:Awake,Beta:Awake,"
                                "Alpha:Start,Beta:Start,"
                                "Alpha:Update,Beta:Update,"
                                "Alpha:LateUpdate,Beta:LateUpdate");

            // A second turn repeats only what repeats.
            run.ClearLines();
            Fluxion_Scene_Tick(scene, 0.5f);
            TEST_CHECK(ctx, run.Joined() == "Alpha:Update,Beta:Update,Alpha:LateUpdate,Beta:LateUpdate");

            // Update was handed exactly what the turn was given.
            TEST_CHECK(ctx, InvokeFloat(ctx, run.Machine(), alphaInstance, alpha, "LastDelta") == 0.5f);
            TEST_CHECK(ctx, InvokeInt(ctx, run.Machine(), alphaInstance, alpha, "Ticks") == 2);

            run.ClearLines();
            Fluxion_Scene_Tick(scene, 0.125f);
            TEST_CHECK(ctx, InvokeFloat(ctx, run.Machine(), alphaInstance, alpha, "LastDelta") == 0.125f);
        }
    }

    // --- A component attached part-way through waits its turn -----------

    {
        ScriptedScene run;
        if (run.Start(ctx, "attached-between-turns", kRecorderSource))
        {
            const FluxionSceneHandle scene = run.Scene();
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "object");

            const u32 alpha = Fluxion::Scene::FindComponentClass(scene, "Alpha");
            const u32 beta = Fluxion::Scene::FindComponentClass(scene, "Beta");

            Fluxion::Scene::AddComponent(scene, object, alpha);
            Fluxion_Scene_Tick(scene, 0.0f);
            TEST_CHECK(ctx, run.Joined() == "Alpha:Awake,Alpha:Start,Alpha:Update,Alpha:LateUpdate");

            run.ClearLines();
            Fluxion::Scene::AddComponent(scene, object, beta);
            Fluxion_Scene_Tick(scene, 0.0f);
            TEST_CHECK(ctx, run.Joined() ==
                                "Beta:Awake,Beta:Start,Alpha:Update,Beta:Update,Alpha:LateUpdate,Beta:LateUpdate");
        }
    }

    // --- Removing a component from inside its own Update ----------------

    {
        const std::string source = std::string(kRecorderSource) +
            "class SelfRemover : Component\n"
            "{\n"
            "    void Update(float deltaTime)\n"
            "    {\n"
            "        Console.WriteLine(\"SelfRemover:Update\");\n"
            "        this.gameObject.RemoveComponent<SelfRemover>();\n"
            "    }\n"
            "    void LateUpdate(float deltaTime) { Console.WriteLine(\"SelfRemover:LateUpdate\"); }\n"
            "    void OnDestroy() { Console.WriteLine(\"SelfRemover:OnDestroy\"); }\n"
            "}\n";

        ScriptedScene run;
        if (run.Start(ctx, "removed-from-inside-update", source))
        {
            const FluxionSceneHandle scene = run.Scene();
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "object");

            const u32 alpha = Fluxion::Scene::FindComponentClass(scene, "Alpha");
            const u32 remover = Fluxion::Scene::FindComponentClass(scene, "SelfRemover");
            const u32 beta = Fluxion::Scene::FindComponentClass(scene, "Beta");

            Fluxion::Scene::AddComponent(scene, object, alpha);
            Fluxion::Scene::AddComponent(scene, object, remover);
            Fluxion::Scene::AddComponent(scene, object, beta);

            Fluxion_Scene_Tick(scene, 0.0f);

            // The one that asked to go still finished its own Update, the
            // one after it was reached exactly once, and being told it was
            // going happened once the step was done with every component
            // it was walking -- never in the middle of the walk.
            TEST_CHECK(ctx, run.Joined() ==
                                "Alpha:Awake,Beta:Awake,"
                                "Alpha:Start,Beta:Start,"
                                "Alpha:Update,SelfRemover:Update,Beta:Update,SelfRemover:OnDestroy,"
                                "Alpha:LateUpdate,Beta:LateUpdate");
            TEST_CHECK(ctx, !Fluxion::Scene::HasComponent(scene, object, remover));
            TEST_CHECK(ctx, Fluxion::Scene::ComponentCount(scene, object) == 2);

            // And the two left behind keep running.
            run.ClearLines();
            Fluxion_Scene_Tick(scene, 0.0f);
            TEST_CHECK(ctx, run.Joined() == "Alpha:Update,Beta:Update,Alpha:LateUpdate,Beta:LateUpdate");
        }
    }

    // --- Destroying a game object from inside an Update -----------------

    {
        const std::string source = std::string(kRecorderSource) +
            "class Destroyer : Component\n"
            "{\n"
            "    void Update(float deltaTime)\n"
            "    {\n"
            "        Console.WriteLine(\"Destroyer:Update\");\n"
            "        this.gameObject.Destroy();\n"
            "    }\n"
            "    void LateUpdate(float deltaTime) { Console.WriteLine(\"Destroyer:LateUpdate\"); }\n"
            "    void OnDestroy() { Console.WriteLine(\"Destroyer:OnDestroy\"); }\n"
            "}\n";

        ScriptedScene run;
        if (run.Start(ctx, "object-destroyed-from-inside-update", source))
        {
            const FluxionSceneHandle scene = run.Scene();
            FluxionGameObjectHandle doomed = Fluxion_Scene_CreateGameObject(scene, "doomed");
            FluxionGameObjectHandle bystander = Fluxion_Scene_CreateGameObject(scene, "bystander");

            const u32 alpha = Fluxion::Scene::FindComponentClass(scene, "Alpha");
            const u32 destroyer = Fluxion::Scene::FindComponentClass(scene, "Destroyer");
            const u32 beta = Fluxion::Scene::FindComponentClass(scene, "Beta");

            Fluxion::Scene::AddComponent(scene, doomed, alpha);
            Fluxion::Scene::AddComponent(scene, doomed, destroyer);
            Fluxion::Scene::AddComponent(scene, bystander, beta);

            Fluxion_Scene_Tick(scene, 0.0f);

            // Everything the step was walking was still walked, both
            // components on the object that went were told, and the one
            // on the object beside it is untouched.
            TEST_CHECK(ctx, run.Joined() ==
                                "Alpha:Awake,Beta:Awake,"
                                "Alpha:Start,Beta:Start,"
                                "Alpha:Update,Destroyer:Update,Beta:Update,"
                                "Alpha:OnDestroy,Destroyer:OnDestroy,"
                                "Beta:LateUpdate");
            TEST_CHECK(ctx, !Fluxion_GameObject_IsValid(scene, doomed));
            TEST_CHECK(ctx, Fluxion_GameObject_IsValid(scene, bystander));
            TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(scene) == 1);

            run.ClearLines();
            Fluxion_Scene_Tick(scene, 0.0f);
            TEST_CHECK(ctx, run.Joined() == "Beta:Update,Beta:LateUpdate");
        }
    }

    // --- A component is the scene's, and a collection may not take it ---

    {
        const std::string source = std::string(kRecorderSource) +
            "class Junk { int a; int b; Junk(int v) { this.a = v; this.b = v; } }\n"
            "static class Waste\n"
            "{\n"
            "    static int Churn()\n"
            "    {\n"
            "        for (int i = 0; i < 3000; i += 1) { Junk t = new Junk(i); }\n"
            "        return 3000;\n"
            "    }\n"
            "}\n";

        ScriptedScene run;
        if (run.Start(ctx, "components-outlive-a-collection", source))
        {
            const FluxionSceneHandle scene = run.Scene();
            Vm* vm = run.Machine();

            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "object");
            const u32 alpha = Fluxion::Scene::FindComponentClass(scene, "Alpha");
            const u32 quiet = Fluxion::Scene::FindComponentClass(scene, "Quiet");

            const ObjectHandle alphaInstance = Fluxion::Scene::AddComponent(scene, object, alpha);
            const ObjectHandle quietInstance = Fluxion::Scene::AddComponent(scene, object, quiet);
            TEST_CHECK(ctx, !alphaInstance.IsNull() && !quietInstance.IsNull());

            // Both have run, so both are carrying a value that has to
            // still be there afterwards.
            Fluxion_Scene_Tick(scene, 0.75f);
            Fluxion_Scene_Tick(scene, 0.75f);
            TEST_CHECK(ctx, InvokeInt(ctx, vm, quietInstance, quiet, "Seen") == 2);

            const u32 collectionsBefore = GetHeapStats(vm).collectionCount;

            // Three thousand objects nothing keeps, then a collection with
            // nothing running -- the moment a component the scene holds
            // would go if it were not held.
            auto churned = Invoke(vm, "Waste.Churn");
            TEST_CHECK(ctx, churned.IsOk());
            CollectGarbage(vm);

            const HeapStats stats = GetHeapStats(vm);
            TEST_CHECK(ctx, stats.collectionCount > collectionsBefore);
            TEST_CHECK(ctx, stats.totalAllocations >= 3000);

            // The throwaway objects went and the components did not.
            TEST_CHECK(ctx, stats.liveObjects == 2);
            TEST_CHECK(ctx, IsObjectAlive(vm, alphaInstance));
            TEST_CHECK(ctx, IsObjectAlive(vm, quietInstance));

            // And what they were holding is exactly what they held.
            TEST_CHECK(ctx, InvokeFloat(ctx, vm, alphaInstance, alpha, "LastDelta") == 0.75f);
            TEST_CHECK(ctx, InvokeInt(ctx, vm, alphaInstance, alpha, "Ticks") == 2);
            TEST_CHECK(ctx, InvokeInt(ctx, vm, quietInstance, quiet, "Seen") == 2);

            // They still run, which is the thing a collection taking them
            // would have broken.
            run.ClearLines();
            Fluxion_Scene_Tick(scene, 0.75f);
            TEST_CHECK(ctx, run.Joined() == "Alpha:Update,Alpha:LateUpdate");
            TEST_CHECK(ctx, InvokeInt(ctx, vm, quietInstance, quiet, "Seen") == 3);

            // Once removed, nothing holds them up any more.
            TEST_CHECK(ctx, Fluxion::Scene::RemoveComponent(scene, object, alpha));
            TEST_CHECK(ctx, Fluxion::Scene::RemoveComponent(scene, object, quiet));
            CollectGarbage(vm);
            TEST_CHECK(ctx, !IsObjectAlive(vm, alphaInstance));
            TEST_CHECK(ctx, !IsObjectAlive(vm, quietInstance));
        }
    }

    // --- Reaching a component by its type -------------------------------

    {
        const std::string source = std::string(kRecorderSource) +
            "static class Probe\n"
            "{\n"
            "    static int SeenOn(GameObject go)\n"
            "    {\n"
            "        Quiet q = go.GetComponent<Quiet>();\n"
            "        if (q == null) { return 0 - 1; }\n"
            "        return q.Seen();\n"
            "    }\n"
            "    static bool HasBeta(GameObject go) { return go.HasComponent<Beta>(); }\n"
            "    static bool AddBeta(GameObject go)\n"
            "    {\n"
            "        Beta added = go.AddComponent<Beta>();\n"
            "        return added != null;\n"
            "    }\n"
            "}\n";

        ScriptedScene run;
        if (run.Start(ctx, "components-reached-by-type", source))
        {
            const FluxionSceneHandle scene = run.Scene();
            Vm* vm = run.Machine();

            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "object");
            const u32 quiet = Fluxion::Scene::FindComponentClass(scene, "Quiet");
            const u32 alpha = Fluxion::Scene::FindComponentClass(scene, "Alpha");
            const u32 beta = Fluxion::Scene::FindComponentClass(scene, "Beta");

            const ObjectHandle quietInstance = Fluxion::Scene::AddComponent(scene, object, quiet);
            Fluxion_Scene_Tick(scene, 0.0f);
            Fluxion_Scene_Tick(scene, 0.0f);
            run.ClearLines();

            // What the host asked for is the same object the script gets.
            TEST_CHECK(ctx, Fluxion::Scene::GetComponent(scene, object, quiet) == quietInstance);

            // A type nothing on the object answers to gives nothing back,
            // rather than the nearest thing that happens to be there.
            TEST_CHECK(ctx, Fluxion::Scene::GetComponent(scene, object, alpha).IsNull());
            TEST_CHECK(ctx, !Fluxion::Scene::HasComponent(scene, object, alpha));

            ScriptValue argument;
            argument.type = ValueType::Handle;
            argument.handleValue = EngineHandle{ object.index, object.generation };

            const u32 probeClass = FindClass(vm, "Probe");
            const u32 seenOn = FindMethod(vm, probeClass, "SeenOn");
            const u32 hasBeta = FindMethod(vm, probeClass, "HasBeta");
            const u32 addBeta = FindMethod(vm, probeClass, "AddBeta");
            TEST_CHECK(ctx, seenOn != kNoFunction && hasBeta != kNoFunction && addBeta != kNoFunction);

            // The script reached the very component the scene holds, and
            // called a method on it.
            auto seen = InvokeStatic(vm, seenOn, &argument, 1);
            TEST_CHECK(ctx, seen.IsOk());
            TEST_CHECK(ctx, seen.IsOk() && seen.Value().intValue == 2);

            auto beforeAdding = InvokeStatic(vm, hasBeta, &argument, 1);
            TEST_CHECK(ctx, beforeAdding.IsOk() && !beforeAdding.Value().boolValue);

            // A component made from inside a script: the machine is asked
            // to make an object while it is already running one.
            auto added = InvokeStatic(vm, addBeta, &argument, 1);
            TEST_CHECK(ctx, added.IsOk());
            TEST_CHECK(ctx, added.IsOk() && added.Value().boolValue);
            TEST_CHECK(ctx, Fluxion::Scene::HasComponent(scene, object, beta));

            auto afterAdding = InvokeStatic(vm, hasBeta, &argument, 1);
            TEST_CHECK(ctx, afterAdding.IsOk() && afterAdding.Value().boolValue);

            run.ClearLines();
            Fluxion_Scene_Tick(scene, 0.0f);
            TEST_CHECK(ctx, run.Joined() == "Beta:Awake,Beta:Start,Beta:Update,Beta:LateUpdate");
        }
    }

    // --- What a component may reach the scene through -------------------

    {
        const char* const source =
            "class Mover : Component\n"
            "{\n"
            "    void Start()\n"
            "    {\n"
            "        this.transform.SetLocalPosition(1.0f, 2.0f, 3.0f);\n"
            "        this.gameObject.SetName(\"renamed by a script\");\n"
            "    }\n"
            "    void Update(float deltaTime) { this.transform.Rotate(0.0f, deltaTime, 0.0f); }\n"
            "    float PositionX() { return this.transform.GetLocalPositionX(); }\n"
            "    string OwnerName() { return this.gameObject.GetName(); }\n"
            "    int ChildCount() { return this.gameObject.GetChildCount(); }\n"
            "}\n";

        ScriptedScene run;
        if (run.Start(ctx, "a-component-reaches-its-object", source))
        {
            const FluxionSceneHandle scene = run.Scene();
            Vm* vm = run.Machine();

            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "object");
            FluxionGameObjectHandle child = Fluxion_Scene_CreateGameObject(scene, "child");
            Fluxion_GameObject_SetParent(scene, child, object);

            const u32 mover = Fluxion::Scene::FindComponentClass(scene, "Mover");
            const ObjectHandle instance = Fluxion::Scene::AddComponent(scene, object, mover);
            TEST_CHECK(ctx, !instance.IsNull());

            Fluxion_Scene_Tick(scene, 0.5f);

            // What the script set is what the scene holds.
            TEST_CHECK(ctx, Fluxion_GameObject_GetLocalPosition(scene, object).x == 1.0f);
            TEST_CHECK(ctx, Fluxion_GameObject_GetLocalPosition(scene, object).z == 3.0f);
            TEST_CHECK(ctx, std::string(Fluxion_GameObject_GetName(scene, object)) == "renamed by a script");

            // And what the scene holds is what the script reads back --
            // text out of a native is taken into the machine's own table
            // before the script ever sees it.
            const u32 ownerName = FindMethod(vm, mover, "OwnerName");
            auto named = InvokeMethod(vm, instance, ownerName, nullptr, 0);
            TEST_CHECK(ctx, named.IsOk());
            TEST_CHECK(ctx, named.IsOk() && named.Value().stringValue == "renamed by a script");

            TEST_CHECK(ctx, InvokeFloat(ctx, vm, instance, mover, "PositionX") == 1.0f);
            TEST_CHECK(ctx, InvokeInt(ctx, vm, instance, mover, "ChildCount") == 1);

            // The turn's own rotation was applied to the object.
            TEST_CHECK(ctx, Fluxion_GameObject_GetLocalRotation(scene, object).y != 0.0f);
        }
    }

    // --- What may not be attached ---------------------------------------

    {
        const char* const source =
            "class NotAComponent { int value; }\n"
            "class WrongShape : Component\n"
            "{\n"
            "    void Update() { }\n"
            "}\n";

        ScriptedScene run;
        if (run.Start(ctx, "what-may-not-be-attached", source))
        {
            const FluxionSceneHandle scene = run.Scene();
            FluxionGameObjectHandle object = Fluxion_Scene_CreateGameObject(scene, "object");

            const u32 plain = Fluxion::Scene::FindComponentClass(scene, "NotAComponent");
            TEST_CHECK(ctx, plain != kNoClass);
            TEST_CHECK(ctx, Fluxion::Scene::AddComponent(scene, object, plain).IsNull());
            TEST_CHECK(ctx, std::string(Fluxion_Scene_GetLastError(scene)).find("not built on") != std::string::npos);

            const u32 wrong = Fluxion::Scene::FindComponentClass(scene, "WrongShape");
            TEST_CHECK(ctx, Fluxion::Scene::AddComponent(scene, object, wrong).IsNull());
            TEST_CHECK(ctx, std::string(Fluxion_Scene_GetLastError(scene)).find("Update") != std::string::npos);

            TEST_CHECK(ctx, Fluxion::Scene::ComponentCount(scene, object) == 0);
        }
    }
}
