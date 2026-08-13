#pragma once

#include <Fluxion/Foundation/Handle.h>
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/Foundation/Types.h>

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

// `name` may be null, which names the object the empty string.
FluxionGameObjectHandle Fluxion_Scene_CreateGameObject(FluxionSceneHandle scene, const char* name);

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

#ifdef __cplusplus
}
#endif
