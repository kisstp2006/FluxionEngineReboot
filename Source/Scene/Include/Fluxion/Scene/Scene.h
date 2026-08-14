#pragma once

#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Foundation/Handle.h>
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Foundation/UUID.h>

#ifdef __cplusplus
extern "C" {
#endif

// How many scenes may exist at once, how much each may hold, and how long
// a name may be. Fixed rather than grown: a handle stays valid because
// its record never moves, and a record never moves because the storage
// never reallocates.
#define FLUXION_SCENE_MAX_SCENES 4
#define FLUXION_SCENE_MAX_GAME_OBJECTS 1024
#define FLUXION_SCENE_MAX_COMPONENTS 2048
#define FLUXION_SCENE_MAX_NAME_LENGTH 64

// How many distinct data-component types one scene may hold. Only the
// table of pools is fixed at this; the storage behind each pool is taken
// when the type is first used and given back when the scene goes.
#define FLUXION_SCENE_MAX_COMPONENT_TYPES 32

FLUXION_DEFINE_HANDLE(FluxionSceneHandle);
FLUXION_DEFINE_HANDLE(FluxionGameObjectHandle);

// An invalid handle of either kind, so a caller answering "there is none"
// never has to write the two numbers out.
FluxionSceneHandle Fluxion_Scene_InvalidHandle(void);
FluxionGameObjectHandle Fluxion_GameObject_InvalidHandle(void);

// --- Scenes -------------------------------------------------------------

FluxionSceneHandle Fluxion_Scene_Create(void);

// Destroys every object the scene holds -- which runs OnDestroy on every
// component still attached -- and then the scene itself.
void Fluxion_Scene_Destroy(FluxionSceneHandle scene);

bool Fluxion_Scene_IsValid(FluxionSceneHandle scene);

u32 Fluxion_Scene_GameObjectCount(FluxionSceneHandle scene);

// The objects with no parent, in the order they were created, walked with
// Fluxion_GameObject_GetNextSibling.
FluxionGameObjectHandle Fluxion_Scene_GetFirstRoot(FluxionSceneHandle scene);

// The first object anywhere in the scene with this name, searched from
// the roots downwards. An invalid handle when there is none.
FluxionGameObjectHandle Fluxion_Scene_Find(FluxionSceneHandle scene, const char* name);

// One turn of the whole scene, in this order: every component that has
// not had Awake yet, then every one that has not had Start yet, then
// Update on all of them, then LateUpdate on all of them. A component
// attached while this is running is picked up by the next turn, so a
// component cannot make itself run twice in one.
//
// Removing a component -- or destroying a game object -- from inside any
// of those is safe: what is asked for is recorded and carried out once
// the step that asked has finished with every component it was walking.
void Fluxion_Scene_Tick(FluxionSceneHandle scene, f32 deltaTime);

// What went wrong most recently on this scene, as text, or an empty
// string when nothing has. Set by anything that can refuse: attaching a
// component whose stated requirement is missing, naming a class that is
// not a component, a script method that faulted.
const char* Fluxion_Scene_GetLastError(FluxionSceneHandle scene);

// --- Game objects -------------------------------------------------------

// `name` may be null, which names the object the empty string. The object
// is given a fresh unique id of its own.
FluxionGameObjectHandle Fluxion_Scene_CreateGameObject(FluxionSceneHandle scene, const char* name);

// The same, but with the id supplied rather than made: what reading a
// scene back in needs, so that an object read from a file is the same
// object it was when written, and so that whatever pointed at it still
// finds it.
//
// Refused, with an invalid handle, when the scene already holds an object
// with this id or when the id is nil -- an id that names two objects is
// worse than no id at all.
FluxionGameObjectHandle Fluxion_Scene_CreateGameObjectWithUUID(FluxionSceneHandle scene, const char* name, FluxionUUID uuid);

// Nil for a handle that names no live object.
FluxionUUID Fluxion_GameObject_GetUUID(FluxionSceneHandle scene, FluxionGameObjectHandle object);

// The object of this scene carrying this id, or an invalid handle when
// there is none. Nil is carried by no object and so never found.
FluxionGameObjectHandle Fluxion_Scene_FindByUUID(FluxionSceneHandle scene, FluxionUUID uuid);

// Destroys the object and everything below it. Every component on every
// object destroyed has OnDestroy run on it first.
void Fluxion_GameObject_Destroy(FluxionSceneHandle scene, FluxionGameObjectHandle object);

bool Fluxion_GameObject_IsValid(FluxionSceneHandle scene, FluxionGameObjectHandle object);

// Never null: an object whose handle names nothing answers with the empty
// string. The text belongs to the scene and stays put until the name is
// set again or the object is destroyed.
const char* Fluxion_GameObject_GetName(FluxionSceneHandle scene, FluxionGameObjectHandle object);

// Longer than FLUXION_SCENE_MAX_NAME_LENGTH - 1 is cut to fit rather than
// refused.
void Fluxion_GameObject_SetName(FluxionSceneHandle scene, FluxionGameObjectHandle object, const char* name);

// --- Hierarchy ----------------------------------------------------------

// Moves `object` under `parent`, or, with an invalid `parent`, back up to
// the scene's roots. The object keeps its LOCAL position, rotation and
// scale exactly as they were, so where it ends up in the world changes to
// whatever those now mean under the new parent -- reparenting is a change
// of what the local transform is relative to, not a way of holding the
// object still.
//
// An object cannot be put under itself or under anything already below
// it; such a request is refused and nothing moves.
void Fluxion_GameObject_SetParent(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionGameObjectHandle parent);

FluxionGameObjectHandle Fluxion_GameObject_GetParent(FluxionSceneHandle scene, FluxionGameObjectHandle object);
FluxionGameObjectHandle Fluxion_GameObject_GetFirstChild(FluxionSceneHandle scene, FluxionGameObjectHandle object);
FluxionGameObjectHandle Fluxion_GameObject_GetNextSibling(FluxionSceneHandle scene, FluxionGameObjectHandle object);
u32 Fluxion_GameObject_GetChildCount(FluxionSceneHandle scene, FluxionGameObjectHandle object);

// Among this object's own children only.
FluxionGameObjectHandle Fluxion_GameObject_FindChild(FluxionSceneHandle scene, FluxionGameObjectHandle object, const char* name);

// Anywhere below this object, children first and then their children.
FluxionGameObjectHandle Fluxion_GameObject_FindChildRecursive(FluxionSceneHandle scene, FluxionGameObjectHandle object, const char* name);

// --- Transforms ---------------------------------------------------------

// Every object has exactly one transform, which is part of it rather than
// something attached to it: there is no way to take it away and therefore
// no way to ask for one that is not there.

void Fluxion_GameObject_SetLocalPosition(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionVec3 position);
FluxionVec3 Fluxion_GameObject_GetLocalPosition(FluxionSceneHandle scene, FluxionGameObjectHandle object);

void Fluxion_GameObject_SetLocalRotation(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionQuat rotation);
FluxionQuat Fluxion_GameObject_GetLocalRotation(FluxionSceneHandle scene, FluxionGameObjectHandle object);

void Fluxion_GameObject_SetLocalScale(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionVec3 scale);
FluxionVec3 Fluxion_GameObject_GetLocalScale(FluxionSceneHandle scene, FluxionGameObjectHandle object);

// Turns the object about its own axes, by the given angles in radians,
// applied on top of the rotation it already has.
void Fluxion_GameObject_Rotate(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionVec3 eulerRadians);

// Translation, rotation and scale of this object and of everything above
// it, composed. Worked out when it is asked for and kept until something
// it depends on changes: setting any part of a transform marks that
// object and everything below it as needing to be worked out again.
FluxionMat4 Fluxion_GameObject_GetWorldMatrix(FluxionSceneHandle scene, FluxionGameObjectHandle object);

// The object's own translation, rotation and scale alone.
FluxionMat4 Fluxion_GameObject_GetLocalMatrix(FluxionSceneHandle scene, FluxionGameObjectHandle object);

// --- Data components ----------------------------------------------------

// A data component is a plain struct attached to a game object: no
// behaviour, no lifecycle, nothing called on it. It is named by the id of
// its registered reflected type, and that registration is where its size
// comes from -- there is no second place to state it, and a type that was
// never registered cannot be attached.
//
// Every one of these hands back a pointer into the scene's own storage.
// That pointer is good until the next call that adds or removes a
// component OF THE SAME TYPE in the SAME scene, or until the object is
// destroyed; anything else leaves it alone. Hold the handle across such a
// call and ask again, rather than holding the pointer.

// Attaches a component of `type` and hands back the storage for it. With
// `initialValue` null the component starts as all zero bytes; otherwise
// that many bytes are copied from it. An object already carrying this type
// gets its existing component back untouched -- one of a type per object.
//
// Null when the object is not live, when the type was never registered for
// reflection, or when the scene has no room for another type.
void* Fluxion_GameObject_AddComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionTypeId type, const void* initialValue);

// The object's component of this type, or null when it carries none.
void* Fluxion_GameObject_GetComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionTypeId type);

bool Fluxion_GameObject_HasComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionTypeId type);

// False when the object carries no component of this type, which is not
// an error -- removing what is not there is simply nothing to do.
bool Fluxion_GameObject_RemoveComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, FluxionTypeId type);

// Every component of one type in the scene, packed with no gaps, and the
// object each belongs to in the same order -- element i of the array is
// owned by element i of the owners. Both are null with a count of zero
// when the scene holds none of this type.
//
// The order is not stated and does not stay put: removing a component
// moves the last one into its place. Read them as a set, not as a
// sequence.
void* Fluxion_Scene_GetComponentArray(FluxionSceneHandle scene, FluxionTypeId type, u32* outCount);
const FluxionGameObjectHandle* Fluxion_Scene_GetComponentOwners(FluxionSceneHandle scene, FluxionTypeId type, u32* outCount);

u32 Fluxion_Scene_ComponentCount(FluxionSceneHandle scene, FluxionTypeId type);

#ifdef __cplusplus
}
#endif
