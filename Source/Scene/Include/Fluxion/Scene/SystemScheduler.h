#pragma once

#include <Fluxion/Scene/Scene.h>

#ifdef __cplusplus
extern "C" {
#endif

// Work that runs over a scene once per step, in a stated order, several
// pieces at a time where that is provably safe.
//
// A system says four things about itself: when in the step it belongs,
// what it reads, what it writes, and which other systems it has to run
// before or after. The first decides the coarse order. The last decides
// the fine order. The middle two decide nothing about order at all --
// they decide only whether two systems that are otherwise free to go in
// either order may go at the same time.
//
// That separation is the point. Access alone cannot say which of two
// systems the author meant to go first; ordering alone cannot say
// whether two systems are independent. Both are needed, and neither
// stands in for the other.

// Where in a step a system runs. Systems in an earlier phase have all
// finished before any system in a later one starts, and whatever they
// asked to change structurally has landed.
//
// The list is deliberately short of a physics group: a phase nothing ever
// runs in is a name with no meaning. Adding one later moves the numbers
// after it, which costs nothing because a phase is named in code and
// never written to a file.
typedef enum FluxionSystemPhase
{
    FLUXION_SYSTEM_PHASE_BEGIN_FRAME = 0,
    FLUXION_SYSTEM_PHASE_PRE_SIMULATION,
    FLUXION_SYSTEM_PHASE_SIMULATION,
    FLUXION_SYSTEM_PHASE_POST_SIMULATION,
    FLUXION_SYSTEM_PHASE_TRANSFORM,
    FLUXION_SYSTEM_PHASE_PRE_RENDER,
    FLUXION_SYSTEM_PHASE_RENDER_EXTRACTION,
    FLUXION_SYSTEM_PHASE_END_FRAME,
    FLUXION_SYSTEM_PHASE_COUNT
} FluxionSystemPhase;

// How many systems one scene may hold, and how much any one of them may
// declare. All three bound fixed storage on the scene record; none is a
// limit anyone is expected to reach.
#define FLUXION_SCENE_MAX_SYSTEMS 64
#define FLUXION_SYSTEM_MAX_ACCESS 16
#define FLUXION_SYSTEM_MAX_ORDER_LINKS 8
#define FLUXION_SYSTEM_MAX_NAME_LENGTH 64

typedef void (*FluxionSystemFn)(FluxionSceneHandle scene, f32 deltaTime, void* userData);

typedef struct FluxionSystemDesc
{
    // Not decoration: this is how other systems name this one when they
    // say they must run before or after it. Two systems in one scene may
    // not share a name.
    const char* name;

    FluxionSystemPhase phase;
    FluxionSystemFn run;
    void* userData;

    // The component types this system looks at and the ones it changes.
    // Two systems may run at the same time only when neither writes
    // something the other touches.
    //
    // These are checked against what the system actually does, so an
    // omission is found where it is made rather than as a race much later.
    const FluxionTypeId* reads;
    u32 readCount;
    const FluxionTypeId* writes;
    u32 writeCount;

    // Systems this one must run after, and systems it must run before,
    // named as strings.
    //
    // By name rather than by handle because a system may be registered by
    // a plugin, and a plugin is loaded whenever it is loaded. An order
    // that depended on who registered first would be an order the author
    // of neither system chose. A name that matches nothing registered is
    // simply not an edge -- it is not an error, because the system it
    // names may live in a plugin that is not loaded.
    const char* const* executeAfter;
    u32 executeAfterCount;
    const char* const* executeBefore;
    u32 executeBeforeCount;

    // For a system whose reach cannot honestly be listed -- one that hands
    // control to code outside the engine, which may touch anything. Such a
    // system conflicts with every other and therefore always runs alone.
    //
    // It is not a shortcut for "I did not want to write the list out":
    // declaring it costs all the concurrency in its phase.
    bool exclusive;
} FluxionSystemDesc;

FLUXION_DEFINE_HANDLE(FluxionSystemHandle);

// Adds a system to this scene. An invalid handle when the scene is not
// live, when the description is incomplete, when the name is already
// taken, or when the scene already holds as many systems as it can.
FluxionSystemHandle Fluxion_Scene_AddSystem(FluxionSceneHandle scene, const FluxionSystemDesc* desc);

bool Fluxion_Scene_RemoveSystem(FluxionSceneHandle scene, FluxionSystemHandle system);

u32 Fluxion_Scene_SystemCount(FluxionSceneHandle scene);

// Runs one step: every phase in order, and within a phase the systems in
// the order their declarations require, several at a time where that
// changes nothing.
//
// This is what Fluxion_Scene_Tick does; it is here as well for a caller
// driving a scene without the rest of a step.
void Fluxion_Scene_RunSystems(FluxionSceneHandle scene, f32 deltaTime);

// --- The systems this module puts in itself ------------------------------
//
// Added the first time a scene is stepped. Named here so that a system of
// your own can say it runs before or after one of them, spelled the way
// they are spelled rather than the way they are remembered.
//
// The two script ones are declared as reaching everything, because a
// script may: it is code the engine did not write. They therefore never
// run beside anything else.
extern const char* const FLUXION_SYSTEM_NAME_SCRIPT_SIMULATION;
extern const char* const FLUXION_SYSTEM_NAME_SCRIPT_POST_SIMULATION;
extern const char* const FLUXION_SYSTEM_NAME_TRANSFORM;

// --- What the scheduler will not let a system do -------------------------

// While a system runs, the storage must not move: another system may be
// reading it at that moment, and even alone, a system is usually walking
// the very blocks it would move.
//
// So a system does not add or remove components itself -- it records the
// change into the scene's command buffer, which is played back at the end
// of the phase, before the next one starts. Attempting it directly while a
// system runs is refused.
//
// This is the same rule the storage already states for anyone iterating
// it; a system is simply the case where the engine can tell.

// The two pairs of systems the scheduler could not order: they touch the
// same component type, one of them writes it, and neither says which
// should go first. Their relative order is settled by name so that a run
// is at least repeatable, but it is settled by the engine rather than by
// anyone who thought about it.
//
// Written out for a caller that wants to check, once, that its systems say
// what they mean. Returns how many pairs there are; fills up to `max` of
// them, first names into `outFirst`, second into `outSecond`.
u32 Fluxion_Scene_FindUnorderedSystemPairs(FluxionSceneHandle scene, const char** outFirst, const char** outSecond, u32 max);

#ifdef __cplusplus
}
#endif
