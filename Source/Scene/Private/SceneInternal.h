#pragma once

#include <Fluxion/Scene/EntityCommandBuffer.h>
#include <Fluxion/Scene/Scene.h>
#include <Fluxion/Scene/SystemScheduler.h>
#include <Fluxion/Scene/Transform.h>

#ifdef __cplusplus
extern "C" {
#endif

// The storage behind the two public handles, shared between the part of
// this module written in C -- objects, hierarchy, transforms -- and the
// part that has to be C++ because the scripting runtime's interface is.
// Nothing here is public: a caller reaches all of it through the handles.

#define FLUXION_SCENE_NO_COMPONENT 0xFFFFFFFFu
#define FLUXION_SCENE_NO_ARCHETYPE 0xFFFFFFFFu
#define FLUXION_SCENE_NO_SYSTEM 0xFFFFFFFFu

// Which lifecycle method a cached index belongs to. Resolved once when a
// component is attached, so a turn of the scene costs no name lookup at
// all.
typedef enum FluxionSceneLifecycle
{
    FLUXION_SCENE_LIFECYCLE_AWAKE = 0,
    FLUXION_SCENE_LIFECYCLE_START,
    FLUXION_SCENE_LIFECYCLE_UPDATE,
    FLUXION_SCENE_LIFECYCLE_LATE_UPDATE,
    FLUXION_SCENE_LIFECYCLE_ON_DESTROY,
    FLUXION_SCENE_LIFECYCLE_COUNT
} FluxionSceneLifecycle;

typedef struct FluxionSceneComponentRecord
{
    bool inUse;

    // Asked for while a step was walking every component, and carried out
    // once that step is done with all of them.
    bool removing;

    bool awakePending;
    bool startPending;

    FluxionGameObjectHandle owner;
    u32 classIndex;

    // The script object this component is, as the two numbers the
    // scripting runtime names one by. Held here and pinned for exactly as
    // long as this record is in use.
    u32 instanceIndex;
    u32 instanceGeneration;

    // FLUXION_SCENE_NO_FUNCTION for a lifecycle method the class does not
    // declare, which is how "not called" is said.
    u32 methods[FLUXION_SCENE_LIFECYCLE_COUNT];

    // The next component on the same object, so asking one object what it
    // carries costs no walk of the whole table.
    u32 nextOnOwner;
} FluxionSceneComponentRecord;

typedef struct FluxionSceneGameObjectRecord
{
    bool alive;

    // Destruction was asked for while a step was walking components. The
    // object reports invalid from that moment, but its record is not
    // reused until the components standing on it have had OnDestroy run
    // -- which is what lets OnDestroy still reach the object it was on.
    bool pendingDestroy;

    u32 generation;

    // What this object is called from outside the run it was made in. The
    // handle above says where the object sits in this table right now and
    // means nothing once the program has ended; this stays the same
    // through being written out and read back.
    FluxionUUID uuid;

    char name[FLUXION_SCENE_MAX_NAME_LENGTH];

    FluxionGameObjectHandle parent;
    FluxionGameObjectHandle firstChild;
    FluxionGameObjectHandle nextSibling;

    // How far below a root this object sits, zero for a root itself.
    //
    // Hierarchy rather than transform, so it lives here rather than in the
    // transform component. It is what lets the world-matrix update be
    // split across workers: two objects at the same depth cannot be each
    // other's parent, so a whole depth can be worked out at once.
    //
    // Maintained wherever an object's parent changes, for the object and
    // for everything below it.
    u32 depth;

    // Where this object's data components live: which composition, which
    // of that composition's blocks, and which row of it. Every live object
    // has all three, because an object carrying nothing still belongs to
    // the empty composition -- that is what keeps "an object is somewhere"
    // free of exceptions.
    //
    // All three change whenever the object gains or loses a component, and
    // the last two change when another object is removed from the same
    // block. They are the sparse half of the storage; the dense half is
    // the blocks themselves.
    u32 archetypeIndex;
    u32 chunkIndex;
    u32 rowInChunk;
} FluxionSceneGameObjectRecord;

// --- Storage grouped by composition -------------------------------------
//
// One composition -- one particular set of component types -- and the
// blocks holding the objects that carry exactly that set.
//
// The type list is kept SORTED. That is what makes {A,B} and {B,A} the
// same composition rather than two, and it also fixes the column order,
// so two objects with the same set always find their components in the
// same place.
//
// Column offsets are worked out once, when the composition first arises,
// and are the same in every block of it. Column 0 is always the entities;
// every composition has that one, including the empty composition an
// object starts in.

typedef struct FluxionSceneChunk
{
    // One allocation of FLUXION_SCENE_CHUNK_BYTES holding every column
    // back to back. Nothing else points into it, so it can be given back
    // whole.
    u8* bytes;

    u32 count;
} FluxionSceneChunk;

typedef struct FluxionSceneArchetype
{
    bool inUse;

    FluxionTypeId types[FLUXION_SCENE_MAX_COMPONENT_TYPES];
    u32 typeCount;

    // Per type, parallel to `types`: how wide one value is, and where its
    // column starts inside a block. Sizes come from the reflection
    // registry when the composition arises, so a later change to a type's
    // registration cannot silently reinterpret storage already laid out.
    usize elementSizes[FLUXION_SCENE_MAX_COMPONENT_TYPES];
    usize columnOffsets[FLUXION_SCENE_MAX_COMPONENT_TYPES];

    // Where the entity column starts, and how many rows a block holds.
    usize entityColumnOffset;
    u32 capacity;

    // Blocks, in order, all full except possibly the last. Grown as
    // objects need them; the array of them grows, the blocks themselves
    // never move.
    FluxionSceneChunk* chunks;
    u32 chunkCount;
    u32 chunkCapacity;
} FluxionSceneArchetype;

// One registered system, with everything it declared copied in: the
// caller's arrays only have to outlive the call that registers it.
typedef struct FluxionSceneSystem
{
    bool inUse;
    u32 generation;

    char name[FLUXION_SYSTEM_MAX_NAME_LENGTH];
    FluxionSystemPhase phase;
    FluxionSystemFn run;
    void* userData;
    bool exclusive;

    FluxionTypeId reads[FLUXION_SYSTEM_MAX_ACCESS];
    u32 readCount;
    FluxionTypeId writes[FLUXION_SYSTEM_MAX_ACCESS];
    u32 writeCount;

    char executeAfter[FLUXION_SYSTEM_MAX_ORDER_LINKS][FLUXION_SYSTEM_MAX_NAME_LENGTH];
    u32 executeAfterCount;
    char executeBefore[FLUXION_SYSTEM_MAX_ORDER_LINKS][FLUXION_SYSTEM_MAX_NAME_LENGTH];
    u32 executeBeforeCount;
} FluxionSceneSystem;

// The order the systems of one phase run in, and which of them may run at
// the same time. Worked out from the declarations rather than stored by
// the caller, and only when it has gone stale -- a scene's systems change
// when it is being set up, not while it runs.
typedef struct FluxionSceneSchedule
{
    // Every system of every phase, in the order they run. `phaseBegin[p]`
    // is where phase p starts and `phaseEnd[p]` is one past its end.
    u32 order[FLUXION_SCENE_MAX_SYSTEMS];
    u32 phaseBegin[FLUXION_SYSTEM_PHASE_COUNT];
    u32 phaseEnd[FLUXION_SYSTEM_PHASE_COUNT];
    u32 orderCount;

    // Where each run of systems that may go at once begins, as positions
    // in `order`. A run always lies inside one phase.
    u32 waveBegin[FLUXION_SCENE_MAX_SYSTEMS + 1];
    u32 waveCount;

    bool valid;
} FluxionSceneSchedule;

typedef struct FluxionSceneRecord
{
    bool alive;
    u32 generation;

    // This scene's own handle, so anything holding a record can hand the
    // handle back to the public interface without being told it.
    FluxionSceneHandle self;

    FluxionSceneGameObjectRecord objects[FLUXION_SCENE_MAX_GAME_OBJECTS];
    u32 objectCount;

    FluxionGameObjectHandle firstRoot;

    FluxionSceneComponentRecord components[FLUXION_SCENE_MAX_COMPONENTS];

    // The data components, grouped by composition. Unlike the script
    // components above, these are not records in a shared table: they are
    // columns in blocks, and which block an object is in is decided by
    // what it carries.
    FluxionSceneArchetype archetypes[FLUXION_SCENE_MAX_ARCHETYPES];

    // The composition every object starts in: the one carrying nothing but
    // a transform. Held here so that making an object does not have to
    // search for it. FLUXION_SCENE_NO_ARCHETYPE until the first object.
    u32 baseArchetype;

    // Whether anything has been marked as needing its world matrix worked
    // out again since the last update, and whether the last update
    // actually changed any.
    //
    // The second is not the same question as the first, and both are
    // needed: an object that moved last step and stands still this one has
    // to have its previous-world refreshed once more, or it would keep
    // reporting the motion it had. Once both are false there is nothing
    // left to do and the whole update is skipped.
    bool transformsDirty;
    bool transformsChangedLastUpdate;

    // The systems this scene runs, and the order worked out from what they
    // declared. The order is rebuilt when a system is added or taken away,
    // not every step.
    FluxionSceneSystem systems[FLUXION_SCENE_MAX_SYSTEMS];
    u32 systemCount;
    FluxionSceneSchedule schedule;

    // Which system is running, as an index into `systems`, or
    // FLUXION_SCENE_NO_SYSTEM. Set by the scheduler and read by the
    // component accessors, which is how a system touching something it
    // never declared is caught where it happens.
    //
    // Not per-thread: several systems of one run may be going at once, and
    // any of them touching something undeclared is the same mistake. What
    // it costs is that the check names the run rather than the exact
    // system when a run holds more than one -- which is why it reports the
    // whole run.
    u32 runningSystem;
    u32 runningWaveBegin;
    u32 runningWaveEnd;

    // Whether the built-in systems have been put in. Done when the first
    // step runs rather than when the scene is made, so that a caller
    // adding its own systems first still gets them in the right order.
    bool builtInSystemsAdded;

    // The scene's own place to write structural change down, made the
    // first time something asks for it and played back at the end of every
    // turn.
    FluxionEntityCommandBuffer* commandBuffer;

    // One past the highest component record ever used, so a step walks
    // only as far as the table has ever reached.
    u32 componentHighWater;

    // Set while a step is calling into every component. Anything asked
    // for during that is recorded rather than done.
    bool dispatching;

    // Whether any object is waiting to be freed once the current step is
    // done with the components standing on it.
    bool objectsPendingDestroy;

    // Set while components that are going are being told so, which is
    // what keeps a removal asked for from inside OnDestroy from starting
    // a second walk of the same table.
    bool flushing;

    // Fluxion::Script::Vm*, kept as a bare pointer because this header is
    // C and the machine's type is not.
    void* vm;

    // Resolved once when the runtime is attached: the class every
    // component is built on, and the method that hands a component the
    // object it belongs to.
    u32 componentClass;
    u32 bindMethod;

    // The two arrays of things a script reaches a game object and its
    // transform through, one entry per object record. Allocated by the
    // part of this module written in C++, which is also the only part
    // that knows what they are.
    void* gameObjectViews;
    void* transformViews;

    // What this scene published about its script classes to the type
    // registry. Held here because the registry keeps pointers rather than
    // copies, so this outliving the registration is the whole contract.
    // Rebuilt whenever the classes can have changed.
    void* scriptReflection;

    char lastError[256];
} FluxionSceneRecord;

#define FLUXION_SCENE_NO_FUNCTION 0xFFFFFFFFu

// Null when the handle names no live scene.
FluxionSceneRecord* Fluxion_SceneInternal_Resolve(FluxionSceneHandle scene);

// Null when the handle names no live object of this scene. An object
// whose destruction has been asked for but not yet carried out still
// resolves: the components on it are still to be told they are going.
FluxionSceneGameObjectRecord* Fluxion_SceneInternal_ResolveObject(FluxionSceneRecord* record, FluxionGameObjectHandle object);

void Fluxion_SceneInternal_SetError(FluxionSceneRecord* record, const char* message);

// Marks this object and everything below it as needing to be worked out
// again the next time a world matrix is asked for.
void Fluxion_SceneInternal_MarkWorldDirty(FluxionSceneRecord* record, FluxionGameObjectHandle object);

// Takes the object out of whichever list it is in -- its parent's
// children or the scene's roots -- without touching anything else.
void Fluxion_SceneInternal_Unlink(FluxionSceneRecord* record, FluxionGameObjectHandle object);

// Frees one object record outright. The caller has already dealt with the
// components that were on it and with everything below it.
void Fluxion_SceneInternal_FreeObject(FluxionSceneRecord* record, FluxionGameObjectHandle object);

// Says this object and everything below it are going, without freeing
// anything yet.
void Fluxion_SceneInternal_MarkSubtreePendingDestroy(FluxionSceneRecord* record, FluxionGameObjectHandle object);

// Frees every object that was said to be going. Called once the
// components that were standing on them have been told.
void Fluxion_SceneInternal_FreePendingObjects(FluxionSceneRecord* record);

// --- Data components ----------------------------------------------------

// Puts a newly made object into the composition that carries nothing.
// False when there is no room, in which case the object must not be
// considered made -- an object with no place to be is one that every
// later call would have to check for.
bool Fluxion_SceneArchetype_PlaceNewObject(FluxionSceneRecord* record, FluxionGameObjectHandle object);

// Takes an object out of its block entirely, closing the gap. Called
// where the record is actually released rather than where destruction is
// asked for: object indices are handed out again, so a row left behind
// would surface as components on whoever gets the index next.
void Fluxion_SceneArchetype_RemoveObject(FluxionSceneRecord* record, FluxionGameObjectHandle object);

// Gives back every block of every composition in this scene.
void Fluxion_SceneArchetype_ReleaseScene(FluxionSceneRecord* record);

// Where one type's column starts in one block, or null when this
// composition does not carry that type. Shared by the per-object lookups
// and by the query.
void* Fluxion_SceneArchetype_ColumnAt(const FluxionSceneArchetype* archetype, u32 chunkIndex, FluxionTypeId type);

// One object's value of one type, reached from the record rather than
// from a handle -- for the paths that already hold the record and would
// otherwise resolve the same handle twice.
void* Fluxion_SceneArchetype_ValueOf(FluxionSceneRecord* record, const FluxionSceneGameObjectRecord* entry, FluxionTypeId type);

// --- Transforms ---------------------------------------------------------

// Makes sure the transform type is known to the reflection registry,
// which is where the storage takes its size from. False when the registry
// has not been brought up, which is a startup mistake rather than a
// runtime condition -- without it no object can be made at all.
bool Fluxion_SceneTransform_EnsureRegistered(void);

// This object's transform, or null when the handle names no live object.
FluxionTransform* Fluxion_SceneInternal_Transform(FluxionSceneRecord* record, FluxionGameObjectHandle object);

// The same, for a caller that already has the record entry.
void* Fluxion_SceneInternal_TransformOf(FluxionSceneRecord* record, const FluxionSceneGameObjectRecord* entry);

// Translation, rotation and scale of one transform, composed. Does not
// look at anything above the object.
FluxionMat4 Fluxion_SceneInternal_LocalMatrixOf(const FluxionTransform* transform);

// Works out every world matrix that needs working out, and carries every
// object's world into its previous first. Nothing structural may happen
// while this runs.
void Fluxion_SceneInternal_UpdateTransforms(FluxionSceneRecord* record);

// Says this object and everything below it now sit one level deeper or
// shallower than they did, following a change of parent.
void Fluxion_SceneInternal_UpdateSubtreeDepth(FluxionSceneRecord* record, FluxionGameObjectHandle object, u32 depth);

// --- Systems ------------------------------------------------------------

// Runs every phase, working out the order first if the systems have
// changed since it was last worked out.
void Fluxion_SceneInternal_RunSystems(FluxionSceneRecord* record, f32 deltaTime);

// Puts in the systems this module owns -- the script lifecycle and the
// transform update -- the first time a scene is stepped. Done then rather
// than at creation so a caller adding its own systems first still gets
// them ordered against these.
void Fluxion_SceneInternal_EnsureBuiltInSystems(FluxionSceneRecord* record);

// Whether the system running right now, if any, declared that it touches
// this type. `structural` asks about adding or removing a component,
// which no system may do directly whatever it declared.
//
// True when no system is running, because then there is nobody whose
// declaration to check against.
bool Fluxion_SceneInternal_SystemMayTouch(const FluxionSceneRecord* record, FluxionTypeId type, bool structural);

const char* Fluxion_SceneInternal_RunningSystemName(const FluxionSceneRecord* record);

// The two the script half provides, run as systems of their own.
void Fluxion_SceneComponents_RunSimulation(FluxionSceneHandle scene, f32 deltaTime, void* userData);
void Fluxion_SceneComponents_RunPostSimulation(FluxionSceneHandle scene, f32 deltaTime, void* userData);

// --- The scene's own command buffer -------------------------------------

// Carries out whatever was written down during the turn. Does nothing when
// the scene has never been asked for a buffer.
void Fluxion_SceneInternal_PlaybackCommandBuffer(FluxionSceneRecord* record);

// Gives back the buffer without carrying out what is still in it: the
// scene is going, so there is nothing left for those commands to change.
void Fluxion_SceneInternal_ReleaseCommandBuffer(FluxionSceneRecord* record);

// --- What the C++ half provides to the C half ---------------------------

// Marks every component on this object, and on everything below it, as
// going. Called by the C side because destruction starts there.
void Fluxion_SceneComponents_MarkSubtree(FluxionSceneRecord* record, FluxionGameObjectHandle object);

// Runs OnDestroy on every component marked as going, releases each one's
// hold on the script object, and frees the records. Then frees the
// objects that were waiting on exactly that.
void Fluxion_SceneComponents_Flush(FluxionSceneRecord* record);

// Releases whatever the C++ half allocated for this scene.
void Fluxion_SceneComponents_ReleaseScene(FluxionSceneRecord* record);

#ifdef __cplusplus
}

// The part of this module's private interface that cannot be C, because
// the halves that share it are C++ on both sides.
namespace Fluxion::Scene
{

// The head of an object's list of scripts, which lives in a component of
// the object's own, and the index of the one of a given class.
u32* ScriptListHead(FluxionSceneRecord* record, FluxionSceneGameObjectRecord* entry);
u32 FindComponentIndex(FluxionSceneRecord* record, FluxionGameObjectHandle object, u32 classIndex);

// Describes every component class of the attached machine in the engine's
// own type registry, one property per field, and takes down whatever was
// described before.
//
// Called where the classes can have changed: when a machine is attached,
// and after every reload. A description left standing across a reload
// would describe fields that no longer exist.
void PublishScriptReflection(FluxionSceneRecord* record);
void ReleaseScriptReflection(FluxionSceneRecord* record);

} // namespace Fluxion::Scene
#endif
