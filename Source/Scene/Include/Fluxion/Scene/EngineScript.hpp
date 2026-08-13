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
// Call it after BuildBindingTable and with the same scene: Renderer takes
// the object whose place a mesh is drawn at, and objects belong to a
// scene. Which scene that is, is remembered process-wide, for the same
// reason everything under the next heading is -- so with two scenes
// built against, the one built against last is the one an object handed
// to Renderer.DrawMesh is looked for in.
//
// The last three answer through the renderer set by SetScriptRenderer,
// and Assets answers out of the table the Register calls below fill.
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
