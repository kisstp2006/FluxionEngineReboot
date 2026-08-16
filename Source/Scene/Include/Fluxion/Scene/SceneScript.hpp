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

// What a reload carries over, and what it does not:
//
//   * A component's own fields come across, matched by name and type;
//     a field gone, renamed or retyped starts at the new class's default
//     and is counted in `fieldsDropped`.
//   * A reference field does NOT come across -- the object it names
//     lives in the machine being stood down. It starts null, counted as
//     dropped.
//   * Statics, module-level state and locals are gone with the machine.
//
// A real boundary, not an unfinished one: a component's own state is
// what a reload is for.
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
