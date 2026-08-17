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

#include <Fluxion/Assets/AssetTypeId.h>
#include <Fluxion/Foundation/Serialization/Stream.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraph.h>

#ifdef __cplusplus
extern "C" {
#endif

// WHICH PASSES A FRAME IS MADE OF, AS DATA RATHER THAN AS CODE.
//
// The render graph runtime underneath this decides the order, the
// barriers and what can be skipped; it has always been able to. What it
// could not do was learn its node list from anywhere but a C function,
// which put the render path in the program instead of in a file.
//
// An asset holds two lists and nothing else:
//
//   nodes     which pass types this frame instantiates, and what each
//             instance is called
//   imports   which resources the graph expects to be handed from
//             outside -- a swapchain image, a depth buffer, a shadow
//             atlas
//
// EDGES ARE NOT IN HERE. Each pass type's Setup callback declares what it
// reads and writes, and the runtime derives the order from that; an asset
// that also stated the order would be a second answer that can disagree
// with the first.

#define FLUXION_RENDER_GRAPH_ASSET_TYPE_NAME "RenderGraph"

#define FLUXION_RENDER_GRAPH_ASSET_MAGIC          0x464C5847u // "FLXG"
#define FLUXION_RENDER_GRAPH_ASSET_FORMAT_VERSION 1

// The same length the runtime's own resource and node names use, because
// these become those -- a name this side accepts and that side truncates
// is a name that stops matching what a pass declared.
#define FLUXION_RENDER_GRAPH_ASSET_MAX_NAME_LENGTH 63

// As many nodes as the runtime graph has room for. An asset that held
// more could be read and then not built, which is a failure arriving one
// step later than it could have.
#define FLUXION_RENDER_GRAPH_ASSET_MAX_NODES 64

// Imports are the resources a frame hands in from outside, and a frame
// has few of them -- a target, a depth buffer, an atlas. Generous at
// sixteen, and revisit when a graph really needs more.
#define FLUXION_RENDER_GRAPH_ASSET_MAX_IMPORTS 16

typedef enum FluxionRenderGraphAssetImportKind
{
    FLUXION_RENDER_GRAPH_ASSET_IMPORT_TEXTURE = 0,
    FLUXION_RENDER_GRAPH_ASSET_IMPORT_BUFFER,
} FluxionRenderGraphAssetImportKind;

typedef struct FluxionRenderGraphAssetImport
{
    // The resource name the passes use -- it has to be spelled exactly
    // as the pass that reads or writes it spells it, which is what makes
    // an import a contract rather than a note.
    char name[FLUXION_RENDER_GRAPH_ASSET_MAX_NAME_LENGTH + 1];

    FluxionRenderGraphAssetImportKind kind;
} FluxionRenderGraphAssetImport;

typedef struct FluxionRenderGraphAssetNode
{
    // What this instance is called. Only ever read by a person -- it
    // shows up in the DOT dump and in diagnostics -- but it is what makes
    // two instances of one pass type tellable apart there.
    char name[FLUXION_RENDER_GRAPH_ASSET_MAX_NAME_LENGTH + 1];

    // The key into the pass-type registry. Not checked when the asset is
    // read: an asset can be cooked on a machine where no pass type has
    // been registered at all, and the check belongs where the registry
    // exists -- Fluxion_RenderGraphAsset_Instantiate.
    char passType[FLUXION_RENDER_GRAPH_ASSET_MAX_NAME_LENGTH + 1];
} FluxionRenderGraphAssetNode;

// Flat and self-contained on purpose: no pointers inside, so reading one
// is a single allocation and writing one is the same bytes every time.
typedef struct FluxionRenderGraphAsset
{
    char name[FLUXION_RENDER_GRAPH_ASSET_MAX_NAME_LENGTH + 1];

    FluxionRenderGraphAssetImport imports[FLUXION_RENDER_GRAPH_ASSET_MAX_IMPORTS];
    u32 importCount;

    FluxionRenderGraphAssetNode nodes[FLUXION_RENDER_GRAPH_ASSET_MAX_NODES];
    u32 nodeCount;
} FluxionRenderGraphAsset;

// --- The authored form ----------------------------------------------------
//
// A `.rendergraph` file:
//
//   {
//     "name": "DefaultForward",
//     "imports": [
//       { "name": "ForwardOpaquePass.Color0", "kind": "texture" }
//     ],
//     "nodes": [
//       { "name": "shadow", "type": "ShadowPass" },
//       { "name": "opaque", "type": "ForwardOpaquePass" }
//     ]
//   }
//
// "nodes" is required; "name" and "imports" are optional. A key this
// version does not know is skipped rather than refused, so a file written
// by a later build still loads as far as it makes sense -- but a key it
// DOES know and cannot make sense of (an import with no kind, a node with
// no type) fails the whole parse.
//
// Nothing is written into `outAsset` unless the whole text parsed: a
// half-read graph would draw a half-frame rather than say what was wrong.
bool Fluxion_RenderGraphAsset_ParseText(const char* text, usize length, FluxionRenderGraphAsset* outAsset);

// --- The cooked form ------------------------------------------------------

// Writes the cooked bytes into `stream`, which must be a writer. Paired
// with Read below, so the two directions are one format rather than two
// that can drift.
bool Fluxion_RenderGraphAsset_Write(FluxionStream* stream, const FluxionRenderGraphAsset* asset);

// Reads the cooked bytes back. What comes back is owned by the caller and
// given up with Destroy.
bool Fluxion_RenderGraphAsset_Read(const u8* bytes, usize size, FluxionRenderGraphAsset** outAsset);
void Fluxion_RenderGraphAsset_Destroy(FluxionRenderGraphAsset* asset);

FluxionAssetTypeId Fluxion_RenderGraphAsset_TypeId(void);

// Registers the type with the asset system, so a graph can be asked for
// by id like anything else. No device is involved and no finalize step
// exists: a list of names has nothing to hand to a GPU.
bool Fluxion_RenderGraphAsset_RegisterType(void);
void Fluxion_RenderGraphAsset_UnregisterType(void);

// --- Building a real graph out of one -------------------------------------

// One import, satisfied. Which of the two handles is read comes from the
// kind the asset declared for that name -- so a caller that fills in the
// wrong one is told which resource it was, instead of quietly importing
// nothing.
typedef struct FluxionRenderGraphBinding
{
    const char* name;

    FluxionRHITextureHandle texture;
    FluxionRHIBufferHandle buffer;

    // The state the resource is already in when the graph starts, exactly
    // as Fluxion_RenderGraph_ImportTexture takes it.
    FluxionRHIResourceState currentState;
} FluxionRenderGraphBinding;

// What each node instance is given as its userData.
//
// Asked per node rather than handed once for all of them: today both
// registered pass types want the same renderer pointer, and the first
// pass type that wants something else would otherwise have no way to say
// so.
typedef void* (*FluxionRenderGraphPassUserDataFn)(const char* passTypeName, const char* nodeName, void* context);

typedef struct FluxionRenderGraphInstantiateDesc
{
    const FluxionRenderGraphBinding* bindings;
    u32 bindingCount;

    // NULL means every node is given `context` itself, which is the
    // common case and saves a caller writing a function that ignores both
    // its arguments.
    FluxionRenderGraphPassUserDataFn resolveUserData;
    void* context;
} FluxionRenderGraphInstantiateDesc;

// Imports every declared resource and adds every node, into a graph that
// must be freshly created and not yet compiled.
//
// ALL OF IT OR NONE OF IT, and it says which of these it was:
//   - a node naming a pass type nothing registered
//   - a declared import with no binding
//   - a binding naming something the asset does not declare (a typo,
//     which would otherwise import a resource no pass ever reads)
//   - a binding whose handle is invalid, or is the wrong kind
//
// The graph is left untouched when any of those holds, so a caller can
// report the failure and destroy the graph rather than execute half a
// frame.
bool Fluxion_RenderGraphAsset_Instantiate(const FluxionRenderGraphAsset* asset, FluxionRenderGraph* graph,
                                          const FluxionRenderGraphInstantiateDesc* desc);

#ifdef __cplusplus
}
#endif
