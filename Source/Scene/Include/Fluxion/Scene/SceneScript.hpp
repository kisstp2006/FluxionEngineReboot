#pragma once

#include <Fluxion/Scene/Scene.h>
#include <Fluxion/Script/Diagnostics.hpp>
#include <Fluxion/Script/Runtime/Binding.hpp>
#include <Fluxion/Script/Runtime/Value.hpp>
#include <Fluxion/Script/Runtime/Vm.hpp>

// The part of this module that has to be C++, and only because the
// scripting runtime's own interface is: everything a scene does that does
// not involve a script is declared in Scene.h and written in C.

namespace Fluxion::Scene
{

// --- Making a scene reachable from a script -----------------------------

// Fills `table` with the two types a script sees: `GameObject`, which is
// one of this scene's objects, and `Transform`, which is the position,
// rotation and scale that object intrinsically has. Both are handles: a
// script never holds the object itself.
//
// The table is built for one scene and names it, so the handles a script
// passes around can only mean objects of that scene. Nothing is
// registered anywhere -- the caller hands the table to each compilation
// and to the machine, and owns it.
bool BuildBindingTable(FluxionSceneHandle scene, Script::BindingTable& table, Script::DiagnosticList& outDiagnostics);

// The declarations that only make sense once `GameObject` and `Transform`
// are visible -- the class every component is built on. Hand this to
// CompileOptions::hostPrelude alongside the table above.
const char* ComponentPreludeSource();

// Gives the scene the machine its components run on, and works out once
// which class components are built on and how a component is handed the
// object it belongs to. Fails, saying why through
// Fluxion_Scene_GetLastError, when the module has no such class.
//
// The machine must outlive the scene, or be taken away again with a null
// before it is destroyed.
bool AttachRuntime(FluxionSceneHandle scene, Script::Vm* vm);

Script::Vm* GetRuntime(FluxionSceneHandle scene);

// --- Components ---------------------------------------------------------

// The class index a component type answers to, resolved once and then
// used everywhere -- nothing below takes a name.
u32 FindComponentClass(FluxionSceneHandle scene, const char* className);

// Creates an instance of `classIndex`, hands it the object it belongs to
// and holds onto it for as long as it stays attached. The instance is
// pinned for exactly that long, so a collection never takes a component
// the scene is still going to call into.
//
// Refused, with the reason in Fluxion_Scene_GetLastError, when the class
// is not built on the component class, when a lifecycle method it
// declares has the wrong shape, or when something it says it requires is
// not already on the object.
//
// Awake and Start have not run when this returns: they run at the start
// of the next turn of the scene, whoever asked for the component.
Script::ObjectHandle AddComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, u32 classIndex);

// The instance of `classIndex` on this object, or a null reference when
// there is none. A class derived from the one asked for does not answer:
// what was asked for is what is looked for.
Script::ObjectHandle GetComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, u32 classIndex);

bool HasComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, u32 classIndex);

// True when there was one to remove. OnDestroy runs on it: at once when
// nothing is being walked, and otherwise once the step that is walking
// has finished with every component it was going to reach.
bool RemoveComponent(FluxionSceneHandle scene, FluxionGameObjectHandle object, u32 classIndex);

u32 ComponentCount(FluxionSceneHandle scene, FluxionGameObjectHandle object);

} // namespace Fluxion::Scene
