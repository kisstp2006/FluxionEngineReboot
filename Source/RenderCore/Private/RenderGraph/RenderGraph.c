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

#include "RenderGraphInternal.h"

#include <Fluxion/Core/Diagnostics/Profiler.h>

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FluxionRenderGraph* Fluxion_RenderGraph_Create(FluxionRHIDeviceHandle device)
{
    // Plain malloc, not the engine allocator -- same convention the RHI's
    // own Null backend uses for its host-side bookkeeping (no allocator
    // handle is threaded through this API, and the struct is a one-shot,
    // long-lived allocation, not a hot-path allocation).
    FluxionRenderGraph* graph = (FluxionRenderGraph*)malloc(sizeof(FluxionRenderGraph));
    FLUXION_ASSERT_MSG(graph != NULL, "RenderGraph: out of memory creating a FluxionRenderGraph");
    if (!graph) return NULL;

    memset(graph, 0, sizeof(*graph));
    graph->device = device;
    return graph;
}

void Fluxion_RenderGraph_Destroy(FluxionRenderGraph* graph)
{
    if (!graph) return;
    free(graph);
}

FluxionRenderGraphPassHandle Fluxion_RenderGraph_AddPassFromRegistry(FluxionRenderGraph* graph, const char* passTypeName, void* userData)
{
    FluxionRenderGraphPassHandle invalidHandle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(graph != NULL && passTypeName != NULL);

    const FluxionRenderGraphPassType* passType = Fluxion_RenderGraphPassRegistry_Find(passTypeName);
    if (!passType) return invalidHandle;

    FLUXION_ASSERT_MSG(graph->nodeCount < FLUXION_RENDER_GRAPH_MAX_NODES, "RenderGraph: exceeded FLUXION_RENDER_GRAPH_MAX_NODES");
    if (graph->nodeCount >= FLUXION_RENDER_GRAPH_MAX_NODES) return invalidHandle;

    u32 index = graph->nodeCount++;
    FluxionRenderGraphNode* node = &graph->nodes[index];
    memset(node, 0, sizeof(*node));
    node->passType = passType;
    node->userData = userData;

    // Uniquely-enough name for DOT/diagnostics when nothing more specific
    // (e.g. the JSON loader's per-node instance name) was provided.
    snprintf(node->name, sizeof(node->name), "%s#%u", passType->name, index);

    graph->compiled = false; // topology changed -- any prior Compile is stale

    FluxionRenderGraphPassHandle handle = { index, 0 };
    return handle;
}

FluxionRenderGraphTextureHandle Fluxion_RenderGraph_ImportTexture(FluxionRenderGraph* graph, const char* resourceName, FluxionRHITextureHandle texture, FluxionRHIResourceState currentState)
{
    FluxionRenderGraphTextureHandle invalidHandle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(graph != NULL && resourceName != NULL);

    i32 index = Fluxion_RenderGraphInternal_FindOrCreateTextureSlot(graph, resourceName);
    if (index < 0) return invalidHandle;

    FluxionRenderGraphTextureResource* resource = &graph->textures[(u32)index];
    FLUXION_ASSERT_MSG(!resource->created && !resource->imported, "RenderGraph: ImportTexture called with an already-created-or-imported resource name");
    if (resource->created || resource->imported) return invalidHandle;

    resource->imported = true;
    resource->importedHandle = texture;
    resource->importedInitialState = currentState;

    graph->compiled = false;

    FluxionRenderGraphTextureHandle handle = { (u32)index, 0 };
    return handle;
}

FluxionRenderGraphBufferHandle Fluxion_RenderGraph_ImportBuffer(FluxionRenderGraph* graph, const char* resourceName, FluxionRHIBufferHandle buffer, FluxionRHIResourceState currentState)
{
    FluxionRenderGraphBufferHandle invalidHandle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(graph != NULL && resourceName != NULL);

    i32 index = Fluxion_RenderGraphInternal_FindOrCreateBufferSlot(graph, resourceName);
    if (index < 0) return invalidHandle;

    FluxionRenderGraphBufferResource* resource = &graph->buffers[(u32)index];
    FLUXION_ASSERT_MSG(!resource->created && !resource->imported, "RenderGraph: ImportBuffer called with an already-created-or-imported resource name");
    if (resource->created || resource->imported) return invalidHandle;

    resource->imported = true;
    resource->importedHandle = buffer;
    resource->importedInitialState = currentState;

    graph->compiled = false;

    FluxionRenderGraphBufferHandle handle = { (u32)index, 0 };
    return handle;
}

bool Fluxion_RenderGraph_Compile(FluxionRenderGraph* graph)
{
    FLUXION_ASSERT(graph != NULL);
    // Begin/End by hand rather than a scope guard -- this is a C file.
    // The early returns above the pair keep the pairing trivially right.
    FluxionSourceLocation zoneLocation = { __FILE__, __func__, __LINE__ };
    Fluxion_Profiler_ZoneBegin(&zoneLocation, "RenderGraph.Compile");
    graph->compiled = Fluxion_RenderGraphInternal_Compile(graph);
    Fluxion_Profiler_ZoneEnd();
    return graph->compiled;
}

FluxionRHITextureHandle Fluxion_RenderGraph_ResolveTexture(FluxionRenderGraph* graph, FluxionRenderGraphTextureHandle handle)
{
    FluxionRHITextureHandle invalidHandle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(graph != NULL);
    if (!FLUXION_HANDLE_IS_VALID(handle) || handle.index >= graph->textureCount) return invalidHandle;
    return graph->textures[handle.index].resolvedHandle;
}

FluxionRHIBufferHandle Fluxion_RenderGraph_ResolveBuffer(FluxionRenderGraph* graph, FluxionRenderGraphBufferHandle handle)
{
    FluxionRHIBufferHandle invalidHandle = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_ASSERT(graph != NULL);
    if (!FLUXION_HANDLE_IS_VALID(handle) || handle.index >= graph->bufferCount) return invalidHandle;
    return graph->buffers[handle.index].resolvedHandle;
}
