#pragma once

#include <Fluxion/Scene/Scene.h>
#include <Fluxion/Script/Diagnostics.hpp>
#include <Fluxion/Script/Runtime/Binding.hpp>
#include <Fluxion/Script/Runtime/CompileCache.hpp>
#include <Fluxion/Script/Runtime/Value.hpp>
#include <Fluxion/Script/Runtime/Vm.hpp>
#include <Fluxion/Script/Script.hpp>

#include <string>

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

// --- Putting new code under a scene that is already running -------------

// What to build the replacement out of. The table the options name is the
// same one the scene was described to in the first place: the engine has
// not changed, only the source has.
struct ReloadRequest
{
    std::string source;
    Script::CompileOptions options;

    // Leave the directory empty to compile unconditionally. A reload
    // usually follows an edit, so the interesting hit is the one on the
    // way back to source that was compiled before.
    Script::CompileCacheOptions cache;
};

// What a reload did, and what it could not do.
//
// WHAT A RELOAD CARRIES OVER, AND WHAT IT DOES NOT:
//
//   * The values a component holds in its own fields come with it, matched
//     by name and by type. A field that is gone, renamed, or declared with
//     a different type in the new source is left at whatever the new
//     class starts it at, and counted in `fieldsDropped`.
//
//   * A field holding a reference does not come across. The object it
//     names lives in the machine being stood down, and nothing in the
//     machine taking over is that object -- carrying it would mean
//     rebuilding the whole graph reachable from it against a different set
//     of classes, which is a far larger job than this and is not attempted
//     here. Such a field starts out null and is counted as dropped.
//
//   * Nothing that is not a component's own field survives at all. A
//     static field, anything a class holds on behalf of the whole module,
//     and anything a component reached only through a local variable are
//     all gone with the machine that held them.
//
// This is a real boundary rather than an unfinished one: a component's own
// state is what a reload is for, and everything else is state the new code
// is entitled to start afresh with.
struct ReloadReport
{
    bool reloaded = false;

    u32 componentsCarried = 0;
    u32 fieldsCarried = 0;
    u32 fieldsDropped = 0;

    // The machine the scene was running until this call, with every
    // component the scene was holding up let go of, so a collection on it
    // reclaims them. It is still a valid machine and nothing has been
    // destroyed: the caller releases it with DestroyVm once it is sure
    // nothing of its own still points into it. Null when nothing was
    // replaced, which is every case where this answered false.
    Script::Vm* retired = nullptr;
};

// Puts new code under a scene without stopping it.
//
// Nothing is disturbed until the new source has been compiled, loaded, and
// established to still have every component class the scene has attached,
// each with lifecycle methods of the right shape. If any of that fails the
// scene carries on running exactly what it was running, and the reason --
// with the file and line it was found at, for anything the compiler
// reported -- is in `outDiagnostics`.
//
// Past that point the scene gets a fresh instance of every component it
// had, built by the new code, handed back the field values the old ones
// held, and put back at the same point in its life: a component that had
// already been woken and started is not woken and started again, so a
// reload is not a restart.
bool ReloadRuntime(FluxionSceneHandle scene, const ReloadRequest& request, Script::DiagnosticList& outDiagnostics,
    ReloadReport& outReport);

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
