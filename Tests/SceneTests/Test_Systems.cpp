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

#include <Fluxion/Core/Jobs/JobSystem.h>
#include <Fluxion/Core/Reflection/MethodInfo.h>
#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Scene/EntityCommandBuffer.h>
#include <Fluxion/Scene/EntityQuery.h>
#include <Fluxion/Scene/Scene.h>
#include <Fluxion/Scene/SystemScheduler.h>
#include <Fluxion/Scene/Transform.h>
#include <Fluxion/Scene/World.hpp>

#include <cstring>

// What the scheduler decides, and what it must not be able to decide
// wrongly without anybody noticing.
//
// Almost every mistake here is quiet. A system run in the wrong order
// still runs, and still produces a number; two systems run at once that
// should not have been still finish. So the checks are written against a
// record of what actually happened -- each system writes its own mark as
// it runs -- rather than against whether the end result looked plausible.

namespace
{

// Where the systems write down that they ran, in the order they ran.
constexpr u32 kMaxMarks = 64;
u32 g_marks[kMaxMarks];
u32 g_markCount;

void ResetMarks()
{
    std::memset(g_marks, 0, sizeof(g_marks));
    g_markCount = 0;
}

void Mark(u32 value)
{
    if (g_markCount < kMaxMarks) g_marks[g_markCount++] = value;
}

// Where a mark sits in the record, or kMaxMarks when it never ran.
u32 PositionOf(u32 value)
{
    for (u32 i = 0; i < g_markCount; ++i) if (g_marks[i] == value) return i;
    return kMaxMarks;
}

struct TestCounter
{
    static constexpr auto Name = "TestSystemsCounter";
    u32 value;
};

struct TestOther
{
    static constexpr auto Name = "TestSystemsOther";
    u32 value;
};

FluxionTypeId CounterType() { return Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(TestCounter::Name)); }
FluxionTypeId OtherType() { return Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(TestOther::Name)); }

FluxionTypeInfo MakeTypeInfo(const char* name, usize size)
{
    FluxionTypeInfo info;
    info.name = Fluxion_StringView_FromCStr(name);
    info.id = Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(name));
    info.kind = FLUXION_TYPE_KIND_STRUCT;
    info.size = size;
    info.version = 1;
    info.members = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionPropertyInfo));
    info.methods = Fluxion_Span_Make(nullptr, 0, sizeof(FluxionMethodInfo));
    return info;
}

void RegisterTypes()
{
    static FluxionTypeInfo counter = MakeTypeInfo(TestCounter::Name, sizeof(TestCounter));
    static FluxionTypeInfo other = MakeTypeInfo(TestOther::Name, sizeof(TestOther));
    Fluxion_Reflection_RegisterType(&counter);
    Fluxion_Reflection_RegisterType(&other);
}

// Systems that do nothing but say they ran.
void MarkOne(FluxionSceneHandle, f32, void*) { Mark(1); }
void MarkTwo(FluxionSceneHandle, f32, void*) { Mark(2); }
void MarkThree(FluxionSceneHandle, f32, void*) { Mark(3); }
void MarkFour(FluxionSceneHandle, f32, void*) { Mark(4); }

// Whether one of the two below was inside the other while it ran.
//
// The writer holds a flag up for a while and the reader looks for it. With
// real workers and a scheduler that let them share a run, the writer's
// stretch is long enough that the reader would almost certainly see it --
// so this can miss an overlap, but it cannot invent one. A check that can
// only be wrong in the safe direction is still worth having when the thing
// it guards has no other witness.
volatile bool g_writerInside;
volatile bool g_sawOverlap;

void HoldStill()
{
    // Long enough that another thread starting at the same moment would
    // land inside it, short enough not to be felt.
    volatile u32 spin = 0;
    for (u32 i = 0; i < 200000u; ++i) spin = spin + 1u;
    (void)spin;
}

// One that writes what it declared, and one that reads it afterwards.
void AddToCounters(FluxionSceneHandle scene, f32, void*)
{
    const FluxionTypeId required = CounterType();
    FluxionEntityQueryDesc desc{};
    desc.required = &required;
    desc.requiredCount = 1;

    g_writerInside = true;

    FluxionEntityQuery query = Fluxion_Scene_Query(scene, &desc);
    FluxionEntityChunkView chunk;
    while (Fluxion_EntityQuery_Next(&query, &chunk))
    {
        TestCounter* counters = (TestCounter*)Fluxion_EntityChunk_Column(&chunk, required);
        for (u32 row = 0; row < chunk.count; ++row) counters[row].value += 1u;
    }

    HoldStill();
    g_writerInside = false;
    Mark(10);
}

u32 g_observed;

void ObserveCounters(FluxionSceneHandle scene, f32, void*)
{
    const FluxionTypeId required = CounterType();
    FluxionEntityQueryDesc desc{};
    desc.required = &required;
    desc.requiredCount = 1;

    if (g_writerInside) g_sawOverlap = true;

    g_observed = 0;
    FluxionEntityQuery query = Fluxion_Scene_Query(scene, &desc);
    FluxionEntityChunkView chunk;
    while (Fluxion_EntityQuery_Next(&query, &chunk))
    {
        const TestCounter* counters = (const TestCounter*)Fluxion_EntityChunk_Column(&chunk, required);
        for (u32 row = 0; row < chunk.count; ++row) g_observed += counters[row].value;
    }
    Mark(11);
}

// One that changes what exists -- which no system may do directly, so it
// writes the change down and lets the phase's end carry it out.
void SpawnThroughBuffer(FluxionSceneHandle scene, f32, void*)
{
    FluxionEntityCommandBuffer* buffer = Fluxion_Scene_GetCommandBuffer(scene);
    Fluxion_EntityCommandBuffer_CreateGameObject(buffer, "spawned by a system");
    Mark(20);
}

u32 g_countSeenInNextPhase;

void CountObjects(FluxionSceneHandle scene, f32, void*)
{
    g_countSeenInNextPhase = Fluxion_Scene_GameObjectCount(scene);
    Mark(21);
}

FluxionSystemDesc Describe(const char* name, FluxionSystemPhase phase, FluxionSystemFn run)
{
    FluxionSystemDesc desc;
    std::memset(&desc, 0, sizeof(desc));
    desc.name = name;
    desc.phase = phase;
    desc.run = run;
    return desc;
}

// Every check below, run once per way the work can be spread out.
void Test_Systems_Body(TestContext& ctx)
{
    // --- Phases run in order, whatever order they were added in ---------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        ResetMarks();

        // Added back to front on purpose: nothing about the order they
        // were registered in may show up in the order they run.
        FluxionSystemDesc last = Describe("Zed", FLUXION_SYSTEM_PHASE_END_FRAME, MarkFour);
        FluxionSystemDesc third = Describe("Yankee", FLUXION_SYSTEM_PHASE_PRE_RENDER, MarkThree);
        FluxionSystemDesc second = Describe("Xray", FLUXION_SYSTEM_PHASE_SIMULATION, MarkTwo);
        FluxionSystemDesc first = Describe("Whiskey", FLUXION_SYSTEM_PHASE_BEGIN_FRAME, MarkOne);

        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(Fluxion_Scene_AddSystem(scene, &last)));
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(Fluxion_Scene_AddSystem(scene, &third)));
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(Fluxion_Scene_AddSystem(scene, &second)));
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(Fluxion_Scene_AddSystem(scene, &first)));

        Fluxion_Scene_Tick(scene, 0.016f);

        TEST_CHECK(ctx, g_markCount == 4);
        TEST_CHECK(ctx, PositionOf(1) < PositionOf(2));
        TEST_CHECK(ctx, PositionOf(2) < PositionOf(3));
        TEST_CHECK(ctx, PositionOf(3) < PositionOf(4));

        // A name may only mean one system, or every before/after
        // mentioning it would be ambiguous.
        FluxionSystemDesc clash = Describe("Xray", FLUXION_SYSTEM_PHASE_SIMULATION, MarkOne);
        TEST_CHECK(ctx, !FLUXION_HANDLE_IS_VALID(Fluxion_Scene_AddSystem(scene, &clash)));

        Fluxion_Scene_Destroy(scene);
    }

    // --- Declared order beats the order they were added in --------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        ResetMarks();

        // Both in one phase, neither touching anything, so nothing but
        // the declaration can decide which runs first.
        //
        // The names are chosen so that the fallback -- settling it by name
        // -- would put them the OTHER way round: "Zulu" has to run before
        // "Alpha". A pair named the obvious way would pass this test even
        // if the declaration were ignored entirely, which is worth nothing.
        const char* afterZulu[] = { "Zulu" };

        FluxionSystemDesc zulu = Describe("Zulu", FLUXION_SYSTEM_PHASE_PRE_SIMULATION, MarkOne);
        FluxionSystemDesc alpha = Describe("Alpha", FLUXION_SYSTEM_PHASE_PRE_SIMULATION, MarkTwo);
        alpha.executeAfter = afterZulu;
        alpha.executeAfterCount = 1;

        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(Fluxion_Scene_AddSystem(scene, &alpha)));
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(Fluxion_Scene_AddSystem(scene, &zulu)));

        Fluxion_Scene_Tick(scene, 0.016f);
        TEST_CHECK(ctx, g_markCount == 2);
        TEST_CHECK(ctx, PositionOf(1) < PositionOf(2));

        Fluxion_Scene_Destroy(scene);
    }

    // --- Said the other way round, it means the same --------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        ResetMarks();

        // "I go before you" and "you go after me" are the same edge, and
        // either one alone is enough -- so a pair only has to be written
        // down once, by whichever of the two knows about the other.
        // Again named against the fallback: "Yankee" has to run before
        // "Delta", which is not what settling it by name would give.
        const char* beforeDelta[] = { "Delta" };

        FluxionSystemDesc yankee = Describe("Yankee", FLUXION_SYSTEM_PHASE_PRE_SIMULATION, MarkOne);
        yankee.executeBefore = beforeDelta;
        yankee.executeBeforeCount = 1;
        FluxionSystemDesc delta = Describe("Delta", FLUXION_SYSTEM_PHASE_PRE_SIMULATION, MarkTwo);

        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(Fluxion_Scene_AddSystem(scene, &delta)));
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(Fluxion_Scene_AddSystem(scene, &yankee)));

        Fluxion_Scene_Tick(scene, 0.016f);
        TEST_CHECK(ctx, PositionOf(1) < PositionOf(2));

        // A name nothing answers to is not an edge and not an error: the
        // system it names may live in a plugin that was not loaded.
        const char* afterMissing[] = { "NotLoaded.Something" };
        FluxionSystemDesc lonely = Describe("Foxtrot", FLUXION_SYSTEM_PHASE_PRE_SIMULATION, MarkThree);
        lonely.executeAfter = afterMissing;
        lonely.executeAfterCount = 1;
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(Fluxion_Scene_AddSystem(scene, &lonely)));

        ResetMarks();
        Fluxion_Scene_Tick(scene, 0.016f);
        TEST_CHECK(ctx, PositionOf(3) != kMaxMarks);

        Fluxion_Scene_Destroy(scene);
    }

    // --- A writer and a reader of the same thing, in the stated order ---

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        ResetMarks();

        for (u32 i = 0; i < 3; ++i)
        {
            FluxionEntityHandle entity = Fluxion_Scene_CreateGameObject(scene, "counted");
            Fluxion_GameObject_AddComponent(scene, entity, CounterType(), nullptr);
        }

        const FluxionTypeId counter = CounterType();

        // Both are placed after the scripts, because the scripts are in
        // this phase too and reach everything -- so without saying so,
        // nothing would settle whether these run before or after them.
        // That is what the report below is for, and declaring it is the
        // answer it asks for.
        const char* afterScripts[] = { FLUXION_SYSTEM_NAME_SCRIPT_SIMULATION };
        const char* afterWriter[] = { "Adder" };

        FluxionSystemDesc writer = Describe("Adder", FLUXION_SYSTEM_PHASE_SIMULATION, AddToCounters);
        writer.writes = &counter;
        writer.writeCount = 1;
        writer.executeAfter = afterScripts;
        writer.executeAfterCount = 1;

        FluxionSystemDesc reader = Describe("Observer", FLUXION_SYSTEM_PHASE_SIMULATION, ObserveCounters);
        reader.reads = &counter;
        reader.readCount = 1;
        reader.executeAfter = afterWriter;
        reader.executeAfterCount = 1;

        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(Fluxion_Scene_AddSystem(scene, &reader)));
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(Fluxion_Scene_AddSystem(scene, &writer)));

        g_writerInside = false;
        g_sawOverlap = false;

        // They touch the same thing and one writes it, so the declaration
        // is what keeps them apart -- and the reader must see the writer's
        // work, not the state before it.
        Fluxion_Scene_Tick(scene, 0.016f);
        TEST_CHECK(ctx, PositionOf(10) < PositionOf(11));
        TEST_CHECK(ctx, g_observed == 3);

        Fluxion_Scene_Tick(scene, 0.016f);
        TEST_CHECK(ctx, g_observed == 6);

        // Nothing was left for the engine to decide. The reader was never
        // told about the scripts directly -- it was told to follow the
        // writer, and the writer to follow the scripts -- so this only
        // holds because the check follows the edges all the way.
        TEST_CHECK(ctx, Fluxion_Scene_FindUnorderedSystemPairs(scene, nullptr, nullptr, 0) == 0);

        // One writes what the other reads, so they must never have been
        // inside each other -- which is the whole reason the access sets
        // are declared, and the one property nothing else here would
        // notice the loss of.
        TEST_CHECK(ctx, !g_sawOverlap);

        Fluxion_Scene_Destroy(scene);
    }

    // --- A pair that clashes with nothing said about their order ---------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();

        const FluxionTypeId counter = CounterType();

        // In a phase of their own, so the pair reported is theirs and not
        // one of them against something the module put in.
        FluxionSystemDesc writer = Describe("Golf", FLUXION_SYSTEM_PHASE_PRE_SIMULATION, AddToCounters);
        writer.writes = &counter;
        writer.writeCount = 1;

        FluxionSystemDesc reader = Describe("Hotel", FLUXION_SYSTEM_PHASE_PRE_SIMULATION, ObserveCounters);
        reader.reads = &counter;
        reader.readCount = 1;

        Fluxion_Scene_AddSystem(scene, &writer);
        Fluxion_Scene_AddSystem(scene, &reader);

        // Settled by name so a run is repeatable, but settled by the
        // engine rather than by anyone who thought about it -- so it is
        // reported, and a caller that means to be careful can ask.
        const char* first[4];
        const char* second[4];
        const u32 pairs = Fluxion_Scene_FindUnorderedSystemPairs(scene, first, second, 4);
        TEST_CHECK(ctx, pairs == 1);
        TEST_CHECK(ctx, pairs >= 1 && std::strcmp(first[0], "Golf") == 0);
        TEST_CHECK(ctx, pairs >= 1 && std::strcmp(second[0], "Hotel") == 0);

        Fluxion_Scene_Destroy(scene);
    }

    // --- Two systems that touch nothing in common ------------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        ResetMarks();

        const FluxionTypeId counter = CounterType();
        const FluxionTypeId other = OtherType();

        FluxionSystemDesc onCounter = Describe("India", FLUXION_SYSTEM_PHASE_SIMULATION, MarkOne);
        onCounter.writes = &counter;
        onCounter.writeCount = 1;

        FluxionSystemDesc onOther = Describe("Juliett", FLUXION_SYSTEM_PHASE_SIMULATION, MarkTwo);
        onOther.writes = &other;
        onOther.writeCount = 1;

        // Placed where nothing else runs, so the only pair to consider is
        // these two.
        onCounter.phase = FLUXION_SYSTEM_PHASE_PRE_SIMULATION;
        onOther.phase = FLUXION_SYSTEM_PHASE_PRE_SIMULATION;
        Fluxion_Scene_AddSystem(scene, &onCounter);
        Fluxion_Scene_AddSystem(scene, &onOther);

        // Nothing to report: they cannot get in each other's way, so there
        // is nothing for anybody to have decided.
        TEST_CHECK(ctx, Fluxion_Scene_FindUnorderedSystemPairs(scene, nullptr, nullptr, 0) == 0);

        // And both still run.
        Fluxion_Scene_Tick(scene, 0.016f);
        TEST_CHECK(ctx, PositionOf(1) != kMaxMarks);
        TEST_CHECK(ctx, PositionOf(2) != kMaxMarks);

        Fluxion_Scene_Destroy(scene);
    }

    // --- What a system changes lands after its phase, not during ---------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        ResetMarks();
        g_countSeenInNextPhase = 0xFFFFFFFFu;

        FluxionSystemDesc spawner = Describe("Kilo", FLUXION_SYSTEM_PHASE_SIMULATION, SpawnThroughBuffer);
        FluxionSystemDesc counterSystem = Describe("Lima", FLUXION_SYSTEM_PHASE_POST_SIMULATION, CountObjects);

        Fluxion_Scene_AddSystem(scene, &spawner);
        Fluxion_Scene_AddSystem(scene, &counterSystem);

        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(scene) == 0);
        Fluxion_Scene_Tick(scene, 0.016f);

        // The later phase sees it, because what a phase asks for lands
        // before the next one starts.
        TEST_CHECK(ctx, g_countSeenInNextPhase == 1);
        TEST_CHECK(ctx, Fluxion_Scene_GameObjectCount(scene) == 1);
        TEST_CHECK(ctx, PositionOf(20) < PositionOf(21));

        Fluxion_Scene_Destroy(scene);
    }

    // --- The systems this module puts in itself --------------------------

    {
        FluxionSceneHandle scene = Fluxion_Scene_Create();
        FluxionEntityHandle object = Fluxion_Scene_CreateGameObject(scene, "moved");

        // Three of them, and they are there without anyone adding them.
        TEST_CHECK(ctx, Fluxion_Scene_SystemCount(scene) == 0);
        Fluxion_Scene_Tick(scene, 0.016f);
        TEST_CHECK(ctx, Fluxion_Scene_SystemCount(scene) == 3);

        // And the transform one does its work in its phase: a move made
        // now is worked out by the next step.
        FluxionVec3 position = { 7.0f, 0.0f, 0.0f };
        Fluxion_GameObject_SetLocalPosition(scene, object, position);
        Fluxion_Scene_Tick(scene, 0.016f);
        TEST_CHECK(ctx, Fluxion_GameObject_GetWorldMatrix(scene, object).m[0][3] == 7.0f);
        TEST_CHECK(ctx, Fluxion_GameObject_GetPreviousWorldMatrix(scene, object).m[0][3] == 0.0f);

        // A system can be taken away again, and then it is not run.
        FluxionSystemDesc extra = Describe("Mike", FLUXION_SYSTEM_PHASE_END_FRAME, MarkOne);
        FluxionSystemHandle handle = Fluxion_Scene_AddSystem(scene, &extra);
        TEST_CHECK(ctx, Fluxion_Scene_SystemCount(scene) == 4);

        ResetMarks();
        Fluxion_Scene_Tick(scene, 0.016f);
        TEST_CHECK(ctx, PositionOf(1) != kMaxMarks);

        TEST_CHECK(ctx, Fluxion_Scene_RemoveSystem(scene, handle));
        TEST_CHECK(ctx, !Fluxion_Scene_RemoveSystem(scene, handle));
        TEST_CHECK(ctx, Fluxion_Scene_SystemCount(scene) == 3);

        ResetMarks();
        Fluxion_Scene_Tick(scene, 0.016f);
        TEST_CHECK(ctx, PositionOf(1) == kMaxMarks);

        Fluxion_Scene_Destroy(scene);
    }
}

// --- The same thing said in C++ ------------------------------------------

// Worth its own section because the C++ side is templates, and a template
// nothing uses is not compiled at all -- so without this the convenience
// layer would be neither tested nor even known to build.
void Test_Systems_Cpp(TestContext& ctx)
{
    using Fluxion::Scene::World;

    World world;
    ResetMarks();

    // The access sets are written as types here rather than as ids worked
    // out at the call site, which is the whole point of the layer.
    const FluxionSystemHandle writer = world.AddSystem<World::Reads<>, World::Writes<TestCounter>>(
        "CppAdder", FLUXION_SYSTEM_PHASE_PRE_SIMULATION, AddToCounters);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(writer));

    const char* afterWriter[] = { "CppAdder" };
    const FluxionSystemHandle reader = world.AddOrderedSystem<World::Reads<TestCounter>, World::Writes<>>(
        "CppObserver", FLUXION_SYSTEM_PHASE_PRE_SIMULATION, ObserveCounters, afterWriter);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(reader));

    TEST_CHECK(ctx, world.SystemCount() == 2);

    // Declared through the typed layer, so the same ids must have come out
    // of it that the C side would have used -- otherwise the pair would
    // look independent and nothing would report it either.
    TEST_CHECK(ctx, Fluxion_Scene_FindUnorderedSystemPairs(world.Handle(), nullptr, nullptr, 0) == 0);

    {
        Fluxion::Scene::Entity entity = world.Create("counted");
        TEST_CHECK(ctx, entity.Add<TestCounter>() != nullptr);
    }

    world.Tick(0.016f);
    TEST_CHECK(ctx, PositionOf(10) < PositionOf(11));
    TEST_CHECK(ctx, g_observed == 1);

    // The step also put in the ones this module owns, so the count is
    // these two plus those three -- stated rather than written as a
    // number, because a number would have to be re-guessed every time the
    // module gains one.
    const u32 builtIn = 3;
    TEST_CHECK(ctx, world.SystemCount() == builtIn + 2);

    TEST_CHECK(ctx, world.RemoveSystem(writer));
    TEST_CHECK(ctx, world.SystemCount() == builtIn + 1);
}

} // namespace

void Test_Systems_Run(TestContext& ctx)
{
    RegisterTypes();

    // Three ways the work can be spread out. A schedule that only happens
    // to be right because everything ran on one thread in one order fails
    // one of them.
    Fluxion_JobSystem_Init(0, false);
    Test_Systems_Body(ctx);
    Test_Systems_Cpp(ctx);
    Fluxion_JobSystem_Shutdown();

    Fluxion_JobSystem_Init(0, true);
    Test_Systems_Body(ctx);
    Fluxion_JobSystem_Shutdown();

    Test_Systems_Body(ctx);
}
