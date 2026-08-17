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

#include "TestFramework.h"

#include <Fluxion/Foundation/Handle.hpp>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraph.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>
#include <Fluxion/RenderCore/Renderer/RenderTarget.h>
#include <Fluxion/RenderCore/Renderer/Renderer.h>
#include <Fluxion/RenderCore/Renderer/RenderView.h>
#include <Fluxion/RenderCore/Renderer/ShadowMatrices.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>

#include <cstring>
#include <vector>

// The shadow pass, run for real and read back.
//
// Three things nothing else in this engine has ever asked a backend for,
// all of them here at once: a graphics pipeline with NO colour
// attachment, a viewport naming a part of the target rather than all of
// it, and a depth texture drawn into and then read. Each one is the kind
// that compiles everywhere and works in two places out of three.
//
// What it checks is where the depth landed. A quad is drawn across the
// middle of one tile: inside it the atlas holds the quad's depth, and
// outside it still holds the clear. A viewport that ignored its offset
// puts the quad in the wrong tile; a light matrix transposed the wrong
// way puts it nowhere at all.

namespace
{

struct ShadowVertex
{
    f32 position[3];
    f32 normal[3];
    f32 tangent[4];
    f32 uv[2];
};

// A square around the origin, in the plane the light looks straight
// down onto.
const ShadowVertex kQuad[4] = {
    { { -4.0f, 0.0f, -4.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
    { {  4.0f, 0.0f, -4.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
    { {  4.0f, 0.0f,  4.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
    { { -4.0f, 0.0f,  4.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
};

// Both windings, so whichever way the shadow pipeline's culling counts a
// front face, the quad is drawn.
const u16 kQuadIndices[12] = { 0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2 };

void CheckOnBackend(TestContext* ctx, FluxionRHIBackendType backend, const char* backendName)
{
    if (!Fluxion_RHI_IsBackendAvailable(backend)) return;

    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests", true };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(backend, &instanceDesc);
    if (!FLUXION_HANDLE_IS_VALID(instance)) return;

    FluxionRHIAdapterHandle adapters[8];
    FluxionRHIDeviceHandle device = Fluxion::Foundation::NoHandle<FluxionRHIDeviceHandle>();
    if (Fluxion_RHI_EnumerateAdapters(instance, adapters, 8) != 0)
    {
        FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
        device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    }
    if (!FLUXION_HANDLE_IS_VALID(device))
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No %s device -- the shadow pass was NOT checked on it.", backendName);
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RenderGraphPassRegistry_Init();

    FluxionMeshBufferDesc meshDesc{};
    meshDesc.vertexData = kQuad;
    meshDesc.vertexDataSize = sizeof(kQuad);
    meshDesc.indexData = kQuadIndices;
    meshDesc.indexDataSize = sizeof(kQuadIndices);
    meshDesc.use16BitIndices = true;
    meshDesc.vertexLayout.attributes[0].location = 0;
    meshDesc.vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    meshDesc.vertexLayout.attributes[0].offset = offsetof(ShadowVertex, position);
    meshDesc.vertexLayout.attributes[1].location = 1;
    meshDesc.vertexLayout.attributes[1].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    meshDesc.vertexLayout.attributes[1].offset = offsetof(ShadowVertex, normal);
    meshDesc.vertexLayout.attributes[2].location = 2;
    meshDesc.vertexLayout.attributes[2].format = FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT;
    meshDesc.vertexLayout.attributes[2].offset = offsetof(ShadowVertex, tangent);
    meshDesc.vertexLayout.attributes[3].location = 3;
    meshDesc.vertexLayout.attributes[3].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    meshDesc.vertexLayout.attributes[3].offset = offsetof(ShadowVertex, uv);
    meshDesc.vertexLayout.attributeCount = 4;
    meshDesc.vertexLayout.stride = sizeof(ShadowVertex);
    meshDesc.bounds = FluxionAABB{ FluxionVec3{ -4.0f, 0.0f, -4.0f }, FluxionVec3{ 4.0f, 0.0f, 4.0f } };
    meshDesc.debugName = "ShadowPassGPU.Quad";
    FluxionMeshBufferHandle mesh = Fluxion_MeshBuffer_Create(device, queue, &meshDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(mesh));

    // The same geometry under a second handle: what makes two batches is
    // the mesh being a different one, not the shape being different.
    meshDesc.debugName = "ShadowPassGPU.SecondQuad";
    FluxionMeshBufferHandle secondMesh = Fluxion_MeshBuffer_Create(device, queue, &meshDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(secondMesh));

    FluxionRendererHandle renderer = Fluxion_Renderer_Create(device, queue);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(renderer));

    FluxionRenderViewDesc viewDesc{};
    viewDesc.viewMatrix = Fluxion_Mat4_Identity();
    viewDesc.projectionMatrix = Fluxion_Mat4_Identity();
    viewDesc.viewport.width = 16.0f;
    viewDesc.viewport.height = 16.0f;
    viewDesc.viewport.maxDepth = 1.0f;
    viewDesc.layerMask = 0xFFFFFFFFu;
    FluxionRenderViewHandle view = Fluxion_RenderView_Create(device, &viewDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(view));
    Fluxion_RenderView_UpdateFrameConstants(view);

    // Straight down onto the quad, fitted to a sphere that holds it.
    const FluxionVec3 downwards = { 0.0f, -1.0f, 0.0f };
    const FluxionVec3 centre = { 0.0f, 0.0f, 0.0f };
    const FluxionMat4 lightMatrix = Fluxion_ShadowMatrices_Directional(downwards, centre, 8.0f, 0);

    FluxionRenderViewShadow shadow{};
    shadow.lightViewProjection = lightMatrix;
    shadow.lightIndex = 0;
    shadow.coverTo = 1000.0f;
    TEST_CHECK(ctx, Fluxion_RenderView_SetShadows(view, &shadow, 1) == 1);

    const FluxionRHITextureHandle atlas = Fluxion_RenderView_GetShadowAtlasTexture(view);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(atlas));

    // Checked rather than assumed: everything after this reads as a wrong
    // picture when the real answer is that there was no command list.
    FluxionRHICommandListHandle cmd = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(cmd));
    Fluxion_RHI_CommandList_Begin(cmd);

    FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(device);
    Fluxion_RenderGraph_ImportTexture(graph, "ShadowPass.Atlas", atlas, FLUXION_RHI_RESOURCE_STATE_UNDEFINED);
    Fluxion_RenderGraph_AddPassFromRegistry(graph, "ShadowPass", Fluxion_Renderer_GetForwardOpaquePassUserData(renderer));

    Fluxion_Renderer_BeginFrame(renderer, view);
    const FluxionMat4 identity = Fluxion_Mat4_Identity();
    Fluxion_Renderer_DrawMesh(renderer, mesh, Fluxion::Foundation::NoHandle<FluxionMaterialHandle>(),
                              Fluxion::Foundation::NoHandle<FluxionRenderPipelineHandle>(), &identity);

    // A SECOND BATCH, and it is here for one reason: the first batch's
    // rows begin at zero, so a draw that ignored the batch's own first-
    // object index would look perfectly right with only one of them.
    //
    // Its own mesh, so it cannot join the first batch, and a transform
    // that moves it clear off the quad above -- three units along X,
    // where the light's slab is eight wide. If the base index went
    // missing, this batch would read row zero and draw exactly where the
    // first one already did, and the far half of the tile would stay at
    // the clear.
    FluxionMat4 moved = Fluxion_Mat4_Identity();
    moved.m[0][3] = 3.0f;
    moved.m[1][3] = 2.0f; // and nearer the light, so its depth is its own
    Fluxion_Renderer_DrawMesh(renderer, secondMesh, Fluxion::Foundation::NoHandle<FluxionMaterialHandle>(),
                              Fluxion::Foundation::NoHandle<FluxionRenderPipelineHandle>(), &moved);

    // What turns the draws asked for into rows a shader can read, and
    // the commands that draw them. After the last DrawMesh and before
    // anything draws -- the pass finds nothing to do without it.
    Fluxion_Renderer_UploadScene(renderer, cmd);

    TEST_CHECK(ctx, Fluxion_RenderGraph_Compile(graph));
    Fluxion_RenderGraph_Execute(graph, cmd);
    Fluxion_Renderer_EndFrame(renderer, cmd);

    // Out of the pass and into a buffer this side can look at. The size
    // is asked for rather than assumed -- the engine owns that number.
    u32 atlasSize = 0;
    u32 tileSize = 0;
    Fluxion_RenderView_GetShadowAtlasSize(view, &atlasSize, &tileSize);
    TEST_CHECK(ctx, atlasSize >= 2 * tileSize); // room for the untouched neighbour the check below reads

    const usize rowBytes = (usize)atlasSize * sizeof(f32);
    const usize alignedRow = (rowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) /
                             FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;

    FluxionRHIBufferDesc readbackDesc{ alignedRow * atlasSize,
                                       FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST,
                                       FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU, "ShadowPassGPU.Readback" };
    FluxionRHIBufferHandle readback = Fluxion_RHI_CreateBuffer(device, &readbackDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(readback));

    const FluxionRHIBufferHandle noBuffer = Fluxion::Foundation::NoHandle<FluxionRHIBufferHandle>();
    FluxionRHIBarrier toSource = { atlas, noBuffer, FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE, FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE };
    Fluxion_RHI_CommandList_Barrier(cmd, &toSource, 1);
    Fluxion_RHI_CommandList_CopyTextureToBuffer(cmd, atlas, 0, 0, readback, 0);
    Fluxion_RHI_CommandList_End(cmd);

    FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(device, false);
    Fluxion_RHI_Queue_Submit(queue, &cmd, 1, fence);
    TEST_CHECK(ctx, Fluxion_RHI_WaitForFence(fence));

    if (const u8* mapped = (const u8*)Fluxion_RHI_MapBuffer(readback))
    {
        const u32 tile = tileSize;

        // WHERE EACH QUAD ALONE REACHES, measured rather than assumed.
        //
        // The light's slab is sixteen wide and the quads are eight, so
        // the first one covers the middle half of the tile and the
        // second -- moved three along, which the light's axes turn into
        // the other direction -- covers the near half. Five eighths
        // across is the first one on its own; one eighth is the second
        // one on its own.
        const usize firstOnlyColumn = (usize)((f32)tile * 0.625f);
        f32 insideTile = 0.0f;
        std::memcpy(&insideTile, mapped + (usize)(tile / 2) * alignedRow + firstOnlyColumn * sizeof(f32), sizeof(f32));

        // The middle of the tile beside it, which nothing drew into and
        // which the clear therefore still owns. This is what says the
        // viewport honoured its offset rather than covering everything.
        f32 outsideTile = 0.0f;
        std::memcpy(&outsideTile, mapped + (usize)(tile / 2) * alignedRow + (usize)(tile + tile / 2) * sizeof(f32), sizeof(f32));

        FLUXION_LOG_INFO("RenderCoreTests", "%s: shadow atlas reads %.4f inside the tile against %.4f outside it.",
                         backendName, (f64)insideTile, (f64)outsideTile);

        // The same row counted from the other end. WHICH of the two
        // holds the tile is not the same on every backend: OpenGL stores
        // a texture's rows from the bottom while the others store them
        // from the top, so an atlas written identically comes back
        // upside down there. Measured here rather than argued about --
        // and the sampling step has to turn this into one rule, because
        // a shader computing tile coordinates cannot be right in two
        // orientations at once.
        f32 mirroredTile = 0.0f;
        std::memcpy(&mirroredTile, mapped + (usize)(atlasSize - 1 - tile / 2) * alignedRow + firstOnlyColumn * sizeof(f32), sizeof(f32));

        FLUXION_LOG_INFO("RenderCoreTests", "%s: the tile was found %s.", backendName,
                         (insideTile < 0.9f) ? "counting rows from the top" : "counting rows from the bottom");

        // The quad sits halfway along the light's slab, so it lands at
        // half the depth range -- the SAME number on every backend now,
        // which is what settling on one clip convention bought. Under
        // the old one this read 0.0 on two backends out of three, with
        // the near half of the range quietly discarded.
        const f32 drawnDepth = (insideTile < 0.9f) ? insideTile : mirroredTile;
        TEST_CHECK(ctx, drawnDepth > 0.45f && drawnDepth < 0.55f);

        // WHERE ONLY THE SECOND BATCH REACHES.
        //
        // And where only the SECOND batch reaches. That batch's rows
        // start at one, not zero: a draw that ignored its own
        // first-object index would read row zero here and draw the
        // first quad again -- unmoved, unraised, and nowhere near this
        // column.
        //
        // This is the check that the batch's own first-object index is
        // used: without it the second batch reads row zero, draws where
        // the first one did, and this texel keeps the clear.
        const usize secondColumn = (usize)((f32)tile * 0.125f);
        f32 secondFromTop = 0.0f;
        f32 secondFromBottom = 0.0f;
        std::memcpy(&secondFromTop, mapped + (usize)(tile / 2) * alignedRow + secondColumn * sizeof(f32), sizeof(f32));
        std::memcpy(&secondFromBottom, mapped + (usize)(atlasSize - 1 - tile / 2) * alignedRow + secondColumn * sizeof(f32), sizeof(f32));

        const f32 secondDepth = secondFromTop < secondFromBottom ? secondFromTop : secondFromBottom;
        FLUXION_LOG_INFO("RenderCoreTests", "%s: where only the second batch reaches, the atlas reads %.4f.", backendName, (f64)secondDepth);
        TEST_CHECK(ctx, secondDepth > 0.15f && secondDepth < 0.45f);

        // And the tile beside it, which nothing drew into, still holds
        // the clear. This is what says the viewport honoured its offset
        // rather than covering the whole atlas.
        TEST_CHECK(ctx, outsideTile > 0.99f);

        Fluxion_RHI_UnmapBuffer(readback);
    }

    Fluxion_RenderGraph_Destroy(graph);
    Fluxion_RHI_DestroyBuffer(readback);
    Fluxion_RHI_DestroyFence(fence);
    Fluxion_RHI_DestroyCommandList(cmd);
    Fluxion_RenderView_Destroy(view);
    Fluxion_Renderer_Destroy(renderer);
    Fluxion_MeshBuffer_Destroy(secondMesh);
    Fluxion_MeshBuffer_Destroy(mesh);
    Fluxion_RenderGraphPassRegistry_Shutdown();

    // See the same call in Test_ComparisonSamplingGPU: a destroyed object
    // is only retired, and the pool slot comes back here.
    Fluxion_RHI_Device_CollectGarbage(device);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}

} // namespace

extern "C" void Test_ShadowPassGPU_Run(TestContext* ctx)
{
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No shader compiler on this machine -- the shadow pass was NOT checked here.");
        return;
    }

    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_OPENGL, "OpenGL");
}
