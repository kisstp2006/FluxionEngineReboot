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

#include <Fluxion/RenderCore/Renderer/Material.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>
#include <Fluxion/RenderCore/Renderer/RenderPipeline.h>
#include <Fluxion/RenderCore/Renderer/Renderer.h>
#include <Fluxion/Scene/Scene.h>
#include <Fluxion/Script/Diagnostics.hpp>
#include <Fluxion/Script/Runtime/Binding.hpp>

// The rest of the engine, as a script sees it. SceneScript.hpp makes one
// scene reachable; this makes reachable the things a component written in
// the language needs in order to do anything with a frame -- how much
// time has passed, what the player is holding down, and how to put
// something on the screen.
//
// Only what is genuinely there is here. There is no analogue input by
// name, no typed text and no cursor capture, because the input system
// underneath has none of those; and nothing here creates a mesh, a
// material or a pipeline, because creating one needs a device and a queue
// that belong to whoever brought the renderer up.

namespace Fluxion::Scene
{

// --- What the script can reach ------------------------------------------

// Adds, to a table that already holds `GameObject` and `Transform`:
//
//   Time            how long the last frame took, and how many there have been
//   Input           keys, mouse and gamepads, as they stand this frame
//   Mesh            geometry the host made, named but not otherwise touchable
//   Material        parameters of one of the host's materials, settable
//   RenderPipeline  a pipeline the host made, likewise
//   Assets          how a script names any of those three
//   Renderer        putting one mesh on the screen this frame
//   DebugDraw       lines and outlines, in numbers
//
// Call after BuildBindingTable, with the same scene -- the scene is
// remembered process-wide, so with two scenes the last one built against
// wins. The last three answer through the renderer set by
// SetScriptRenderer; Assets answers from the Register calls below.
bool BuildEngineBindings(FluxionSceneHandle scene, Script::BindingTable& table, Script::DiagnosticList& outDiagnostics);

// The declarations that only make sense once those types are visible: the
// sets of constants that name a key, a mouse button, a gamepad button and
// a gamepad axis, and the shaped forms of the calls the engine can only
// take as separate numbers.
//
// Hand this to CompileOptions::hostPrelude together with
// ComponentPreludeSource(), in either order -- neither mentions the
// other.
const char* EnginePreludeSource();

// --- What the host has to supply ----------------------------------------

// Everything below is process-wide rather than per-scene, because what it
// stands for is: there is one renderer bringing up one device, and the
// things it draws with were made once against that device.

// The renderer that Renderer.DrawMesh and DebugDraw draw through. Both
// only do anything between that renderer's own BeginFrame and EndFrame,
// so a script that draws has to be run inside that window -- see the
// sample for where a scene's turn goes because of it. Passing an invalid
// handle takes the renderer away again, after which both quietly do
// nothing rather than reaching through a handle to a renderer that has
// been destroyed.
void SetScriptRenderer(FluxionRendererHandle renderer);
FluxionRendererHandle GetScriptRenderer(void);

// Puts something the host made under a name a script can ask for. A name
// already registered is replaced. False when the name is empty or too
// long, or when there is no room left for another of that kind.
bool RegisterScriptMesh(const char* name, FluxionMeshBufferHandle mesh);
bool RegisterScriptMaterial(const char* name, FluxionMaterialHandle material);
bool RegisterScriptPipeline(const char* name, FluxionRenderPipelineHandle pipeline);

// Forgets every name registered. The handles themselves are the host's,
// and are neither destroyed nor touched by this.
void ClearScriptAssets(void);

} // namespace Fluxion::Scene
