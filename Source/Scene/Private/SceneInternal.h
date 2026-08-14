#pragma once

#include <Fluxion/Scene/EntityCommandBuffer.h>
#include <Fluxion/Scene/Scene.h>

#ifdef __cplusplus
extern "C" {
#endif

// The storage behind the two public handles, shared between the part of
// this module written in C -- objects, hierarchy, transforms -- and the
// part that has to be C++ because the scripting runtime's interface is.
// Nothing here is public: a caller reaches all of it through the handles.

#define FLUXION_SCENE_NO_COMPONENT 0xFFFFFFFFu

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

    FluxionVec3 localPosition;
    FluxionQuat localRotation;
    FluxionVec3 localScale;

    FluxionMat4 worldMatrix;
    bool worldDirty;

    u32 firstComponent;
} FluxionSceneGameObjectRecord;

// One data-component type's storage. `dense` null means this slot of the
// table stands for no type at all -- that, rather than a separate flag, is
// what tells a used pool from a free one, because storage is what a pool
// is.
typedef struct FluxionSceneComponentPool
{
    FluxionTypeId type;
    usize elementSize;

    // The components themselves, packed, and the object each belongs to in
    // the same order. Both hold `count` entries and have room for one per
    // object record.
    u8* dense;
    FluxionGameObjectHandle* owners;

    // Object index -> which row of `dense`, or FLUXION_SCENE_NO_COMPONENT
    // for an object carrying none of this type. One entry per object
    // record, so the answer costs no search.
    u32* rowOf;

    u32 count;
} FluxionSceneComponentPool;

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

    // The data components, one pool per type in use. Unlike the script
    // components above, these are not records in a shared table: each type
    // keeps its own.
    FluxionSceneComponentPool componentPools[FLUXION_SCENE_MAX_COMPONENT_TYPES];

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

// Takes one component out of one pool, closing the gap by moving the last
// row into it. False when that object carried none of this type.
bool Fluxion_SceneComponentPool_RemoveByIndex(FluxionSceneComponentPool* pool, u32 objectIndex);

// Takes every data component off one object record, whatever their types.
// Called where the record is actually released rather than where
// destruction is asked for: object indices are handed out again, so a
// component left behind would surface on whoever gets the index next.
void Fluxion_SceneComponentPool_RemoveAllOf(FluxionSceneRecord* record, u32 objectIndex);

// Gives back the storage every pool of this scene took.
void Fluxion_SceneComponentPool_ReleaseScene(FluxionSceneRecord* record);

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
#endif
