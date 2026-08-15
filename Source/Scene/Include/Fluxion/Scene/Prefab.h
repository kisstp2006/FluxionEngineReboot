#pragma once

#include <Fluxion/Scene/Scene.h>

#ifdef __cplusplus
extern "C" {
#endif

// A thing that can be put into a scene more than once.
//
// A prefab is a scene written down: an object and everything below it,
// with their components. Nothing about it is a special kind of storage --
// it is the same bytes a saved scene is made of, which is why a prefab
// survives whatever a scene survives and needs no format of its own.
//
// What a copy remembers is the one thing that cannot be worked out again:
// which prefab it came from, and which of that prefab's objects each of
// its own objects corresponds to. Everything else about the relationship
// -- which values were changed since, what to put back, what to push
// across -- is worked out by comparing the two, at the moment it is
// asked.
//
// That is not a shortcut. Recording changes as they happen is the obvious
// alternative and it cannot be done here: a component is written through
// a plain pointer into the storage, so there is no moment to record.

typedef struct FluxionPrefab FluxionPrefab;

// Takes a copy of `root` and everything below it.
//
// The scene it came from is not touched and is not remembered: the prefab
// holds its own copy, and destroying that scene afterwards is fine.
FluxionPrefab* Fluxion_Prefab_CreateFromObject(FluxionSceneHandle scene, FluxionGameObjectHandle root);

void Fluxion_Prefab_Destroy(FluxionPrefab* prefab);

// What this prefab is called. Objects copied from it carry this, so a
// copy can be told which prefab it belongs to without holding a pointer
// to something that may have been destroyed.
FluxionUUID Fluxion_Prefab_GetId(const FluxionPrefab* prefab);

// Puts a copy into a scene and hands back its root.
//
// Every object of the copy gets an id of its own -- a copy is a new
// object, and two objects answering to one id would make every lookup by
// id a coin toss. References inside the prefab are remapped to point
// within the copy; references out of it name nothing.
FluxionEntityHandle Fluxion_Prefab_Instantiate(const FluxionPrefab* prefab, FluxionSceneHandle scene);

// Whether this object's component holds something other than what the
// prefab says it should.
//
// False when the object came from no prefab, from a different one, or
// when the type is one the prefab's copy of that object does not carry.
bool Fluxion_Prefab_IsOverridden(const FluxionPrefab* prefab, FluxionSceneHandle scene,
                                 FluxionEntityHandle object, FluxionTypeId type);

// Puts the prefab's values back over this object's, undoing every change
// made since it was copied. The object keeps its own id and its place in
// the scene -- only what its components hold changes.
bool Fluxion_Prefab_Revert(const FluxionPrefab* prefab, FluxionSceneHandle scene, FluxionEntityHandle object);

// The other direction: what this object holds becomes what the prefab
// says. Copies made afterwards get the new values; copies already made
// keep theirs until they are reverted.
bool Fluxion_Prefab_Apply(FluxionPrefab* prefab, FluxionSceneHandle scene, FluxionEntityHandle object);

// --- What is and is not carried ------------------------------------------
//
// Native data components are carried by all four of the above. Script
// components are not: what a script component holds lives in a machine
// belonging to one scene, and moving it to another means building a new
// instance of the class there. That is a larger question than a prefab,
// and answering it badly would be worse than saying so.

#ifdef __cplusplus
}
#endif
