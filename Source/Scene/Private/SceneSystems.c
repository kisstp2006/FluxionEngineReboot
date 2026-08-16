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

// Running a scene's systems: in what order, and how many at once.
//
// Three separate questions, answered by three separate declarations, and
// the whole design rests on not letting any of them stand in for another.
//
//   the phase       decides the coarse order
//   before/after    decides the fine order
//   reads/writes    decides only whether two systems that are free to go
//                   in either order may go at the SAME TIME
//
// Access cannot settle order. If one system writes what another reads,
// that says they must not overlap; it says nothing about which the author
// meant to run first, and picking one silently would be picking for them.
//
// Order cannot settle access either. Two systems with no stated order
// might still touch the same thing, and then the order they happen to get
// changes the answer -- which is why the pairs in that position are
// reported rather than quietly resolved.
//
// Ordering is by NAME rather than by handle because systems can come from
// plugins, and a plugin is loaded when it is loaded. An order that fell
// out of who registered first would be an order nobody chose.

#include "SceneInternal.h"

#include <Fluxion/Core/Jobs/JobSystem.h>
#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Log.h>

#include <string.h>

static const char* const kLogChannel = "Scene.Systems";

// --- Registering ---------------------------------------------------------

static void Fluxion_SceneSystems_CopyName(char* destination, const char* name)
{
    usize length;
    if (name == NULL) { destination[0] = '\0'; return; }

    length = strlen(name);
    if (length >= FLUXION_SYSTEM_MAX_NAME_LENGTH) length = FLUXION_SYSTEM_MAX_NAME_LENGTH - 1;
    memcpy(destination, name, length);
    destination[length] = '\0';
}

static u32 Fluxion_SceneSystems_FindByName(const FluxionSceneRecord* record, const char* name)
{
    u32 i;
    if (name == NULL) return FLUXION_SCENE_NO_SYSTEM;

    for (i = 0; i < FLUXION_SCENE_MAX_SYSTEMS; ++i)
    {
        if (!record->systems[i].inUse) continue;
        if (strcmp(record->systems[i].name, name) == 0) return i;
    }
    return FLUXION_SCENE_NO_SYSTEM;
}

FluxionSystemHandle Fluxion_Scene_AddSystem(FluxionSceneHandle scene, const FluxionSystemDesc* desc)
{
    FluxionSystemHandle handle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneSystem* system;
    u32 index;
    u32 i;

    if (record == NULL || desc == NULL) return handle;
    if (desc->run == NULL || desc->name == NULL || desc->name[0] == '\0') return handle;
    if (desc->phase >= FLUXION_SYSTEM_PHASE_COUNT) return handle;
    if (desc->readCount > FLUXION_SYSTEM_MAX_ACCESS || desc->writeCount > FLUXION_SYSTEM_MAX_ACCESS) return handle;
    if (desc->executeAfterCount > FLUXION_SYSTEM_MAX_ORDER_LINKS) return handle;
    if (desc->executeBeforeCount > FLUXION_SYSTEM_MAX_ORDER_LINKS) return handle;

    // Refused rather than allowed: a duplicate name would make every
    // before/after mentioning it ambiguous, and the whole ordering scheme
    // rests on a name meaning one system.
    if (Fluxion_SceneSystems_FindByName(record, desc->name) != FLUXION_SCENE_NO_SYSTEM)
    {
        Fluxion_SceneInternal_SetError(record, "this scene already holds a system with that name");
        return handle;
    }

    for (index = 0; index < FLUXION_SCENE_MAX_SYSTEMS; ++index)
    {
        if (!record->systems[index].inUse) break;
    }
    if (index == FLUXION_SCENE_MAX_SYSTEMS)
    {
        Fluxion_SceneInternal_SetError(record, "this scene already holds as many systems as one may hold");
        return handle;
    }

    system = &record->systems[index];
    {
        const u32 generation = system->generation + 1;
        memset(system, 0, sizeof(*system));
        system->generation = generation;
    }

    Fluxion_SceneSystems_CopyName(system->name, desc->name);
    system->phase = desc->phase;
    system->run = desc->run;
    system->userData = desc->userData;
    system->exclusive = desc->exclusive;

    // Copied in, so the caller's arrays only have to outlive this call --
    // a system is usually described by a local built where it is
    // registered.
    system->readCount = desc->readCount;
    for (i = 0; i < desc->readCount; ++i) system->reads[i] = desc->reads[i];
    system->writeCount = desc->writeCount;
    for (i = 0; i < desc->writeCount; ++i) system->writes[i] = desc->writes[i];

    system->executeAfterCount = desc->executeAfterCount;
    for (i = 0; i < desc->executeAfterCount; ++i) Fluxion_SceneSystems_CopyName(system->executeAfter[i], desc->executeAfter[i]);
    system->executeBeforeCount = desc->executeBeforeCount;
    for (i = 0; i < desc->executeBeforeCount; ++i) Fluxion_SceneSystems_CopyName(system->executeBefore[i], desc->executeBefore[i]);

    system->inUse = true;
    ++record->systemCount;
    record->schedule.valid = false;

    handle.index = index;
    handle.generation = system->generation;
    return handle;
}

bool Fluxion_Scene_RemoveSystem(FluxionSceneHandle scene, FluxionSystemHandle system)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    FluxionSceneSystem* entry;

    if (record == NULL || system.index >= FLUXION_SCENE_MAX_SYSTEMS) return false;

    entry = &record->systems[system.index];
    if (!entry->inUse || entry->generation != system.generation) return false;

    entry->inUse = false;
    if (record->systemCount != 0) --record->systemCount;
    record->schedule.valid = false;
    return true;
}

u32 Fluxion_Scene_SystemCount(FluxionSceneHandle scene)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    return record != NULL ? record->systemCount : 0u;
}

// --- Working out the order -----------------------------------------------

// Whether one system named this type at all, reading or writing. What
// matters at the places this is asked is whether the type was declared,
// not in which of the two lists it appeared.
static bool Fluxion_SceneSystems_Reads(const FluxionSceneSystem* system, FluxionTypeId type)
{
    u32 i;
    for (i = 0; i < system->readCount; ++i) if (system->reads[i] == type) return true;
    for (i = 0; i < system->writeCount; ++i) if (system->writes[i] == type) return true;
    return false;
}

// Two systems clash when one changes something the other so much as looks
// at. Two readers of the same thing do not clash: reading is not a change,
// and letting readers overlap is most of what the declarations buy.
static bool Fluxion_SceneSystems_Conflict(const FluxionSceneSystem* a, const FluxionSceneSystem* b)
{
    u32 i;

    // One that cannot say what it touches has to be assumed to touch
    // everything, because it hands control to code this engine did not
    // write.
    if (a->exclusive || b->exclusive) return true;

    for (i = 0; i < a->writeCount; ++i) if (Fluxion_SceneSystems_Reads(b, a->writes[i])) return true;
    for (i = 0; i < b->writeCount; ++i) if (Fluxion_SceneSystems_Reads(a, b->writes[i])) return true;
    return false;
}

// Whether `before` said it must run before `after`, or `after` said it
// must run after `before`. Both spellings mean the same edge; either one
// alone is enough, so a pair only has to be written down once and by
// whichever of the two knows about the other.
static bool Fluxion_SceneSystems_HasEdge(const FluxionSceneSystem* before, const FluxionSceneSystem* after)
{
    u32 i;
    for (i = 0; i < before->executeBeforeCount; ++i) if (strcmp(before->executeBefore[i], after->name) == 0) return true;
    for (i = 0; i < after->executeAfterCount; ++i) if (strcmp(after->executeAfter[i], before->name) == 0) return true;
    return false;
}

// The systems of one phase, in the order they will run.
//
// Kahn's algorithm over the stated edges, and where several are ready at
// once the one whose name sorts first is taken. That tie-break is what
// makes a run repeatable: without it the order would follow the order the
// systems happened to be registered in, and a plugin loading at a
// different moment would change what the program does.
static u32 Fluxion_SceneSystems_OrderPhase(FluxionSceneRecord* record, FluxionSystemPhase phase, u32* out)
{
    u32 members[FLUXION_SCENE_MAX_SYSTEMS];
    u32 memberCount = 0;
    u32 remaining[FLUXION_SCENE_MAX_SYSTEMS];
    u32 written = 0;
    u32 i, j;

    for (i = 0; i < FLUXION_SCENE_MAX_SYSTEMS; ++i)
    {
        if (!record->systems[i].inUse || record->systems[i].phase != phase) continue;
        members[memberCount++] = i;
    }
    if (memberCount == 0) return 0;

    // How many of this phase's systems each one is still waiting on.
    for (i = 0; i < memberCount; ++i)
    {
        u32 waiting = 0;
        for (j = 0; j < memberCount; ++j)
        {
            if (i == j) continue;
            if (Fluxion_SceneSystems_HasEdge(&record->systems[members[j]], &record->systems[members[i]])) ++waiting;
        }
        remaining[i] = waiting;
    }

    while (written < memberCount)
    {
        u32 chosen = memberCount;

        for (i = 0; i < memberCount; ++i)
        {
            if (remaining[i] != 0 || members[i] == FLUXION_SCENE_NO_SYSTEM) continue;
            if (chosen == memberCount ||
                strcmp(record->systems[members[i]].name, record->systems[members[chosen]].name) < 0)
            {
                chosen = i;
            }
        }

        if (chosen == memberCount)
        {
            // Everything still left is waiting on something also still
            // left: the declarations form a ring. Said out loud and then
            // broken by name, because refusing to run the phase at all
            // would turn a mistake in one system's declaration into a
            // scene that does nothing.
            FLUXION_LOG_ERROR(kLogChannel,
                "the systems of one phase declare an order that runs in a circle; the rest of the phase runs in name order");
            for (i = 0; i < memberCount; ++i)
            {
                if (members[i] == FLUXION_SCENE_NO_SYSTEM) continue;
                if (chosen == memberCount ||
                    strcmp(record->systems[members[i]].name, record->systems[members[chosen]].name) < 0)
                {
                    chosen = i;
                }
            }
            if (chosen == memberCount) break;
        }

        out[written++] = members[chosen];
        for (i = 0; i < memberCount; ++i)
        {
            if (members[i] == FLUXION_SCENE_NO_SYSTEM || i == chosen) continue;
            if (Fluxion_SceneSystems_HasEdge(&record->systems[members[chosen]], &record->systems[members[i]]) && remaining[i] != 0)
            {
                --remaining[i];
            }
        }
        members[chosen] = FLUXION_SCENE_NO_SYSTEM;
        remaining[chosen] = 0xFFFFFFFFu;
    }

    return written;
}

// Which systems of a phase have to wait for which, once the stated edges
// are followed as far as they go.
//
// Following them all the way matters: "A before B" and "B before C"
// together settle A against C just as firmly as saying so outright would,
// and a check that only looked at what was written down directly would
// call that pair undecided and ask its author to decide it again.
//
// One bit per system, so the whole answer for a phase of sixty-four fits
// in sixty-four words. Filled from the back of the order forwards, where
// everything a system leads to is already known.
static void Fluxion_SceneSystems_Reachability(FluxionSceneRecord* record, u32 begin, u32 end, u64* reaches)
{
    u32 i, j;

    for (i = end; i > begin; --i)
    {
        const u32 self = i - 1u;
        u64 bits = 0;

        for (j = self + 1u; j < end; ++j)
        {
            const FluxionSceneSystem* before = &record->systems[record->schedule.order[self]];
            const FluxionSceneSystem* after = &record->systems[record->schedule.order[j]];

            if (!Fluxion_SceneSystems_HasEdge(before, after)) continue;
            bits |= (1ull << (j - begin));
            bits |= reaches[j - begin];
        }
        reaches[self - begin] = bits;
    }
}

// Splits an already-ordered phase into runs that may go at once.
//
// A system joins the run being built when it clashes with nothing in it
// and nothing in it was declared to come first. Otherwise a new run
// starts. Not the best possible split -- a full dependency graph would do
// better -- but one whose result can be read off the declarations by
// hand, which matters more for something that decides what runs beside
// what.
static void Fluxion_SceneSystems_BuildWaves(FluxionSceneRecord* record, u32 begin, u32 end)
{
    FluxionSceneSchedule* schedule = &record->schedule;
    u32 waveStart = begin;
    u32 i, j;

    for (i = begin; i < end; ++i)
    {
        bool joins = (i != waveStart);

        for (j = waveStart; j < i && joins; ++j)
        {
            const FluxionSceneSystem* member = &record->systems[schedule->order[j]];
            const FluxionSceneSystem* candidate = &record->systems[schedule->order[i]];

            if (Fluxion_SceneSystems_Conflict(member, candidate)) joins = false;
            else if (Fluxion_SceneSystems_HasEdge(member, candidate)) joins = false;
        }

        if (joins) continue;

        if (i != waveStart) schedule->waveBegin[schedule->waveCount++] = waveStart;
        waveStart = i;
    }

    if (waveStart < end) schedule->waveBegin[schedule->waveCount++] = waveStart;
}

// Says which pairs touch the same thing with nothing to say which goes
// first. Their order is settled by name, which makes a run repeatable but
// does not make it considered -- so it is worth hearing about once,
// where the systems were registered, rather than as a difference in
// behaviour much later.
static void Fluxion_SceneSystems_ReportUnordered(FluxionSceneRecord* record, u32 begin, u32 end)
{
    u64 reaches[FLUXION_SCENE_MAX_SYSTEMS];
    u32 i, j;

    Fluxion_SceneSystems_Reachability(record, begin, end, reaches);

    for (i = begin; i < end; ++i)
    {
        for (j = i + 1; j < end; ++j)
        {
            const FluxionSceneSystem* first = &record->systems[record->schedule.order[i]];
            const FluxionSceneSystem* second = &record->systems[record->schedule.order[j]];

            if (!Fluxion_SceneSystems_Conflict(first, second)) continue;
            if ((reaches[i - begin] & (1ull << (j - begin))) != 0) continue;

            FLUXION_LOG_WARN(kLogChannel,
                "'%s' and '%s' touch the same component and nothing settles which runs first; the order is settled by name",
                first->name, second->name);
        }
    }
}

static void Fluxion_SceneSystems_Rebuild(FluxionSceneRecord* record)
{
    FluxionSceneSchedule* schedule = &record->schedule;
    u32 phase;

    schedule->orderCount = 0;
    schedule->waveCount = 0;

    for (phase = 0; phase < FLUXION_SYSTEM_PHASE_COUNT; ++phase)
    {
        const u32 begin = schedule->orderCount;
        const u32 written = Fluxion_SceneSystems_OrderPhase(record, (FluxionSystemPhase)phase, schedule->order + begin);

        schedule->orderCount += written;
        schedule->phaseBegin[phase] = begin;
        schedule->phaseEnd[phase] = schedule->orderCount;

        if (written == 0) continue;
        Fluxion_SceneSystems_BuildWaves(record, begin, schedule->orderCount);
        Fluxion_SceneSystems_ReportUnordered(record, begin, schedule->orderCount);
    }

    schedule->waveBegin[schedule->waveCount] = schedule->orderCount;
    schedule->valid = true;
}

u32 Fluxion_Scene_FindUnorderedSystemPairs(FluxionSceneHandle scene, const char** outFirst, const char** outSecond, u32 max)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    u32 found = 0;
    u32 phase;

    if (record == NULL) return 0;
    if (!record->schedule.valid) Fluxion_SceneSystems_Rebuild(record);

    for (phase = 0; phase < FLUXION_SYSTEM_PHASE_COUNT; ++phase)
    {
        u64 reaches[FLUXION_SCENE_MAX_SYSTEMS];
        u32 i, j;

        Fluxion_SceneSystems_Reachability(record, record->schedule.phaseBegin[phase], record->schedule.phaseEnd[phase], reaches);

        for (i = record->schedule.phaseBegin[phase]; i < record->schedule.phaseEnd[phase]; ++i)
        {
            for (j = i + 1; j < record->schedule.phaseEnd[phase]; ++j)
            {
                const FluxionSceneSystem* first = &record->systems[record->schedule.order[i]];
                const FluxionSceneSystem* second = &record->systems[record->schedule.order[j]];

                if (!Fluxion_SceneSystems_Conflict(first, second)) continue;
                if ((reaches[i - record->schedule.phaseBegin[phase]] & (1ull << (j - record->schedule.phaseBegin[phase]))) != 0) continue;

                if (found < max)
                {
                    if (outFirst != NULL) outFirst[found] = first->name;
                    if (outSecond != NULL) outSecond[found] = second->name;
                }
                ++found;
            }
        }
    }
    return found;
}

// --- Running -------------------------------------------------------------

typedef struct FluxionSceneSystemWave
{
    FluxionSceneRecord* record;
    const u32* systemIndices;
    f32 deltaTime;
} FluxionSceneSystemWave;

static void Fluxion_SceneSystems_RunOne(void* userData, u32 slot)
{
    FluxionSceneSystemWave* wave = (FluxionSceneSystemWave*)userData;
    FluxionSceneSystem* system = &wave->record->systems[wave->systemIndices[slot]];

    if (!system->inUse || system->run == NULL) return;
    system->run(wave->record->self, wave->deltaTime, system->userData);
}

void Fluxion_SceneInternal_RunSystems(FluxionSceneRecord* record, f32 deltaTime)
{
    u32 phase;
    u32 wave;

    if (record == NULL) return;
    if (!record->schedule.valid) Fluxion_SceneSystems_Rebuild(record);
    if (record->schedule.orderCount == 0) return;

    wave = 0;
    for (phase = 0; phase < FLUXION_SYSTEM_PHASE_COUNT; ++phase)
    {
        const u32 begin = record->schedule.phaseBegin[phase];
        const u32 end = record->schedule.phaseEnd[phase];

        while (wave < record->schedule.waveCount && record->schedule.waveBegin[wave] < end)
        {
            const u32 waveStart = record->schedule.waveBegin[wave];
            const u32 waveEnd = (wave + 1 < record->schedule.waveCount && record->schedule.waveBegin[wave + 1] < end)
                ? record->schedule.waveBegin[wave + 1] : end;
            const u32 count = waveEnd - waveStart;
            FluxionSceneSystemWave batch;

            batch.record = record;
            batch.systemIndices = record->schedule.order + waveStart;
            batch.deltaTime = deltaTime;

            // Remembered for as long as the run lasts, so that the
            // component accessors can tell whether what they are being
            // asked for was declared.
            record->runningWaveBegin = waveStart;
            record->runningWaveEnd = waveEnd;
            record->runningSystem = record->schedule.order[waveStart];

            if (count > 1 && Fluxion_JobSystem_IsInitialized())
            {
                const FluxionJobHandle handle = Fluxion_JobSystem_ParallelFor(
                    count, 1, Fluxion_SceneSystems_RunOne, &batch, NULL, 0);
                Fluxion_JobSystem_Wait(handle);
            }
            else
            {
                u32 slot;
                for (slot = 0; slot < count; ++slot) Fluxion_SceneSystems_RunOne(&batch, slot);
            }

            record->runningSystem = FLUXION_SCENE_NO_SYSTEM;
            ++wave;
        }

        (void)begin;

        // Whatever the phase asked to change structurally lands now, so
        // the next phase sees it -- and so nothing moves the storage while
        // a system is still walking it.
        Fluxion_SceneInternal_PlaybackCommandBuffer(record);
    }
}

void Fluxion_Scene_RunSystems(FluxionSceneHandle scene, f32 deltaTime)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    if (record == NULL) return;

    Fluxion_SceneInternal_EnsureBuiltInSystems(record);
    Fluxion_SceneInternal_RunSystems(record, deltaTime);
}

// --- Checking that a system only touches what it declared ----------------

bool Fluxion_SceneInternal_SystemMayTouch(const FluxionSceneRecord* record, FluxionTypeId type, bool structural)
{
    u32 i;

    if (record == NULL || record->runningSystem == FLUXION_SCENE_NO_SYSTEM) return true;

    // Adding or taking away a component moves the storage, which is the
    // one thing that must not happen while anything is walking it. A
    // system records such a change into the command buffer instead, and it
    // lands at the end of the phase.
    if (structural) return false;

    for (i = record->runningWaveBegin; i < record->runningWaveEnd; ++i)
    {
        const FluxionSceneSystem* system = &record->systems[record->schedule.order[i]];
        if (system->exclusive) return true;
        if (Fluxion_SceneSystems_Reads(system, type)) return true;
    }
    return false;
}

const char* Fluxion_SceneInternal_RunningSystemName(const FluxionSceneRecord* record)
{
    if (record == NULL || record->runningSystem == FLUXION_SCENE_NO_SYSTEM) return "";
    return record->systems[record->runningSystem].name;
}

// --- The systems this module owns ----------------------------------------

// Names the built-in systems answer to. Written down once here so that
// anything ordering itself against them spells them the same way, and so
// that a caller can read them out of a header rather than guess.
const char* const FLUXION_SYSTEM_NAME_SCRIPT_SIMULATION = "Fluxion.ScriptSimulation";
const char* const FLUXION_SYSTEM_NAME_SCRIPT_POST_SIMULATION = "Fluxion.ScriptPostSimulation";
const char* const FLUXION_SYSTEM_NAME_TRANSFORM = "Fluxion.TransformUpdate";

static void Fluxion_SceneSystems_RunTransform(FluxionSceneHandle scene, f32 deltaTime, void* userData)
{
    FluxionSceneRecord* record = Fluxion_SceneInternal_Resolve(scene);
    (void)deltaTime;
    (void)userData;
    Fluxion_SceneInternal_UpdateTransforms(record);
}

void Fluxion_SceneInternal_EnsureBuiltInSystems(FluxionSceneRecord* record)
{
    FluxionSystemDesc desc;
    FluxionTypeId transformType;

    if (record == NULL || record->builtInSystemsAdded) return;
    record->builtInSystemsAdded = true;

    transformType = Fluxion_Transform_TypeId();

    // The scripts. Declared as reaching everything, because a script may:
    // it is code this engine did not write, calling whatever the bindings
    // expose. So it never runs beside anything else, which is the honest
    // price of not being able to say what it touches.
    //
    // Split in two rather than run as one, because the two halves belong
    // to different phases: what a script does every step is simulation,
    // and what it does after everything else has moved is not.
    memset(&desc, 0, sizeof(desc));
    desc.name = FLUXION_SYSTEM_NAME_SCRIPT_SIMULATION;
    desc.phase = FLUXION_SYSTEM_PHASE_SIMULATION;
    desc.run = Fluxion_SceneComponents_RunSimulation;
    desc.exclusive = true;
    (void)Fluxion_Scene_AddSystem(record->self, &desc);

    memset(&desc, 0, sizeof(desc));
    desc.name = FLUXION_SYSTEM_NAME_SCRIPT_POST_SIMULATION;
    desc.phase = FLUXION_SYSTEM_PHASE_POST_SIMULATION;
    desc.run = Fluxion_SceneComponents_RunPostSimulation;
    desc.exclusive = true;
    (void)Fluxion_Scene_AddSystem(record->self, &desc);

    // The world matrices, in the phase named after them. It writes the
    // transform and reads nothing else, so anything in that phase which
    // does not touch transforms may run beside it.
    memset(&desc, 0, sizeof(desc));
    desc.name = FLUXION_SYSTEM_NAME_TRANSFORM;
    desc.phase = FLUXION_SYSTEM_PHASE_TRANSFORM;
    desc.run = Fluxion_SceneSystems_RunTransform;
    desc.writes = &transformType;
    desc.writeCount = 1;
    (void)Fluxion_Scene_AddSystem(record->self, &desc);
}
