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
#include <string.h>

void Fluxion_RenderGraph_Execute(FluxionRenderGraph* graph, FluxionRHICommandListHandle commandList)
{
    FLUXION_ASSERT(graph != NULL);
    FLUXION_ASSERT_MSG(graph->compiled, "Fluxion_RenderGraph_Execute called on a graph that has not been successfully Compiled");
    if (!graph->compiled) return;

    // Covers every pass's Execute callback and every barrier the
    // compiled plan emits -- the whole of command recording, which is
    // the frame's CPU cost besides submission itself.
    FluxionSourceLocation zoneLocation = { __FILE__, __func__, __LINE__ };
    Fluxion_Profiler_ZoneBegin(&zoneLocation, "RenderGraph.Execute");

    // An imported resource's RHI handle is already resolved -- it exists
    // for the whole graph's lifetime, not just one Execute call.
    for (u32 i = 0; i < graph->textureCount; ++i)
    {
        if (graph->textures[i].imported)
        {
            graph->textures[i].resolvedHandle = graph->textures[i].importedHandle;
        }
    }
    for (u32 i = 0; i < graph->bufferCount; ++i)
    {
        if (graph->buffers[i].imported)
        {
            graph->buffers[i].resolvedHandle = graph->buffers[i].importedHandle;
        }
    }

    for (u32 orderPos = 0; orderPos < graph->nodeCount; ++orderPos)
    {
        u32 nodeIndex = graph->executionOrder[orderPos];
        FluxionRenderGraphNode* node = &graph->nodes[nodeIndex];

        // A culled node never owns a resource's create/destroy point or
        // any barrier (both are only ever derived from a surviving,
        // non-culled usage) -- skipping it entirely here is safe.
        if (node->culled) continue;

        // Create this frame's transient resources whose lifetime starts here.
        for (u32 t = 0; t < graph->textureCount; ++t)
        {
            if (!graph->textures[t].imported && graph->createTextureAtNode[t] == nodeIndex)
            {
                graph->textures[t].resolvedHandle = Fluxion_RHI_CreateTexture(graph->device, &graph->textures[t].desc);
            }
        }
        for (u32 b = 0; b < graph->bufferCount; ++b)
        {
            if (!graph->buffers[b].imported && graph->createBufferAtNode[b] == nodeIndex)
            {
                graph->buffers[b].resolvedHandle = Fluxion_RHI_CreateBuffer(graph->device, &graph->buffers[b].desc);
            }
        }

        // Issue the barriers the compiler decided this node needs, one at
        // a time -- resolving each plan's graph resource reference to the
        // real, now-valid RHI handle right before use.
        FluxionRHITextureHandle invalidTexture = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        FluxionRHIBufferHandle invalidBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        u32 barrierStart = graph->barrierStartForNode[nodeIndex];
        u32 barrierCount = graph->barrierCountForNode[nodeIndex];
        for (u32 i = 0; i < barrierCount; ++i)
        {
            const FluxionRenderGraphBarrierPlan* plan = &graph->barriers[barrierStart + i];

            FluxionRHIBarrier barrier;

            memset(&barrier, 0, sizeof(barrier));
            barrier.texture = invalidTexture;
            barrier.buffer = invalidBuffer;
            barrier.before = plan->before;
            barrier.after = plan->after;
            if (plan->kind == FLUXION_RENDER_GRAPH_RESOURCE_TEXTURE)
            {
                barrier.texture = graph->textures[plan->resourceIndex].resolvedHandle;
            }
            else
            {
                barrier.buffer = graph->buffers[plan->resourceIndex].resolvedHandle;
            }

            Fluxion_RHI_CommandList_Barrier(commandList, &barrier, 1);
        }

        node->passType->execute(commandList, node->userData);

        // Destroy this frame's transient resources whose lifetime ends here.
        for (u32 t = 0; t < graph->textureCount; ++t)
        {
            if (!graph->textures[t].imported && graph->destroyTextureAtNode[t] == nodeIndex)
            {
                Fluxion_RHI_DestroyTexture(graph->textures[t].resolvedHandle);
                graph->textures[t].resolvedHandle = invalidTexture;
            }
        }
        for (u32 b = 0; b < graph->bufferCount; ++b)
        {
            if (!graph->buffers[b].imported && graph->destroyBufferAtNode[b] == nodeIndex)
            {
                Fluxion_RHI_DestroyBuffer(graph->buffers[b].resolvedHandle);
                graph->buffers[b].resolvedHandle = invalidBuffer;
            }
        }
    }

    Fluxion_Profiler_ZoneEnd();
}
