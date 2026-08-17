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
#include <Fluxion/RenderCore/Pipeline/RenderGraphAsset.h>
#include <Fluxion/RenderCore/Pipeline/RenderPipelineAsset.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraph.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>
#include <Fluxion/RenderCore/Renderer/Renderer.h>
#include <Fluxion/RenderCore/Renderer/RenderView.h>
#include <Fluxion/RenderCore/Renderer/ShadowMatrices.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>

#include <cstring>

// THE PIPELINE ASSET DECIDES WHAT THE PICTURE IS, MEASURED ON A DEVICE.
//
// The same scene, the same code, the same command list -- and two render
// pipeline assets. One has a shadow pass in its graph and asks for an
// atlas to draw it into; the other has neither. Nothing between them
// changes here except which asset is read.
//
// What is read back is the shadow atlas, because that is where the
// difference lands: with the pass, the middle of the first tile holds
// the quad's depth; without it, the same texel still holds the clear --
// which is what "everything is lit" is stored as.
//
// Test_RenderPipelineAsset.c checks the numbers a quality level maps to.
// This one checks that the mapping reaches a device at all.

namespace
{

struct PipelineVertex
{
    f32 position[3];
    f32 normal[3];
    f32 tangent[4];
    f32 uv[2];
};

// A square around the origin, in the plane the light looks straight
// down onto -- the same one the shadow pass test uses, for the same
// reason: it is the simplest thing that lands at a known depth.
const PipelineVertex kQuad[4] = {
    { { -4.0f, 0.0f, -4.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
    { {  4.0f, 0.0f, -4.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
    { {  4.0f, 0.0f,  4.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
    { { -4.0f, 0.0f,  4.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
};

const u16 kQuadIndices[12] = { 0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2 };

// A pass that claims the atlas and draws nothing into it. It stands in
// for whatever a shadowless pipeline's graph does instead -- what
// matters is that the graph is a real graph with a node in it, and that
// the node is not the shadow pass.
void NothingPassSetup(FluxionRenderGraphBuilder* builder, void* userData)
{
    (void)userData;
    Fluxion_RenderGraphBuilder_WriteDepthTarget(builder, FLUXION_RENDER_VIEW_SHADOW_ATLAS_RESOURCE);
}

void NothingPassExecute(FluxionRHICommandListHandle commandList, void* userData)
{
    (void)commandList;
    (void)userData;
}

const char* const kShadowedGraphText =
    "{"
    "  \"name\": \"Test_Shadowed\","
    "  \"imports\": [ { \"name\": \"ShadowPass.Atlas\", \"kind\": \"texture\" } ],"
    "  \"nodes\": [ { \"name\": \"shadow\", \"type\": \"ShadowPass\" } ]"
    "}";

const char* const kShadowlessGraphText =
    "{"
    "  \"name\": \"Test_Shadowless\","
    "  \"imports\": [ { \"name\": \"ShadowPass.Atlas\", \"kind\": \"texture\" } ],"
    "  \"nodes\": [ { \"name\": \"nothing\", \"type\": \"Test_PipelineNothingPass\" } ]"
    "}";

const char* const kShadowedPipelineText = "{ \"name\": \"Test_Shadowed\", \"graph\": \"Test_Shadowed\", \"shadowQuality\": \"medium\" }";
const char* const kShadowlessPipelineText = "{ \"name\": \"Test_Shadowless\", \"graph\": \"Test_Shadowless\", \"shadowQuality\": \"off\" }";

bool ResolveAnyGraph(const char* graphName, FluxionUUID* outGraphId, void* context)
{
    (void)graphName;
    (void)context;

    // This test does not go through the asset database: what it is
    // checking is what the two assets DO, not how they are found. A
    // reference that is set is all the parse needs from here.
    *outGraphId = Fluxion_UUID_Generate();
    return true;
}

struct RunResult
{
    bool ran = false;
    u32 atlasSize = 0;
    u32 tileSize = 0;
    f32 centreDepth = 0.0f;
};

// One frame through one pipeline asset, with what came out of it.
RunResult DrawThroughPipeline(TestContext* ctx, FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue, FluxionMeshBufferHandle mesh,
                              const char* pipelineText, const char* graphText)
{
    RunResult result;

    FluxionRenderPipelineAsset pipelineAsset{};
    if (!Fluxion_RenderPipelineAsset_ParseText(pipelineText, std::strlen(pipelineText), ResolveAnyGraph, nullptr, &pipelineAsset))
    {
        TEST_CHECK(ctx, false);
        return result;
    }

    FluxionRenderGraphAsset graphAsset{};
    if (!Fluxion_RenderGraphAsset_ParseText(graphText, std::strlen(graphText), &graphAsset))
    {
        TEST_CHECK(ctx, false);
        return result;
    }

    FluxionRendererHandle renderer = Fluxion_Renderer_Create(device, queue);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(renderer));

    FluxionRenderViewDesc viewDesc{};
    viewDesc.viewMatrix = Fluxion_Mat4_Identity();
    viewDesc.projectionMatrix = Fluxion_Mat4_Identity();
    viewDesc.viewport.width = 16.0f;
    viewDesc.viewport.height = 16.0f;
    viewDesc.viewport.maxDepth = 1.0f;
    viewDesc.layerMask = 0xFFFFFFFFu;

    // The one line that makes this a pipeline-driven frame rather than a
    // hand-built one: the atlas the view is made with is the atlas the
    // asset asked for.
    Fluxion_RenderPipelineAsset_ApplyToViewDesc(&pipelineAsset, &viewDesc);

    FluxionRenderViewHandle view = Fluxion_RenderView_Create(device, &viewDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(view));
    Fluxion_RenderView_UpdateFrameConstants(view);

    const FluxionVec3 downwards = { 0.0f, -1.0f, 0.0f };
    const FluxionVec3 centre = { 0.0f, 0.0f, 0.0f };

    FluxionRenderViewShadow shadow{};
    shadow.lightViewProjection = Fluxion_ShadowMatrices_Directional(downwards, centre, 8.0f, 0);
    shadow.lightIndex = 0;
    shadow.coverTo = 1000.0f;

    // Asked for either way. A pipeline whose graph has no shadow pass
    // still has lights that would cast one, and what makes the picture
    // shadowless is that nothing draws into the atlas -- not that the
    // scene stopped asking.
    TEST_CHECK(ctx, Fluxion_RenderView_SetShadows(view, &shadow, 1) == 1);

    Fluxion_RenderView_GetShadowAtlasSize(view, &result.atlasSize, &result.tileSize);

    const FluxionRHITextureHandle atlas = Fluxion_RenderView_GetShadowAtlasTexture(view);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(atlas));

    FluxionRHICommandListHandle cmd = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(cmd));
    Fluxion_RHI_CommandList_Begin(cmd);

    // Clears the atlas the first time, which is what "everything is lit"
    // is stored as -- and what the shadowless run is left holding.
    Fluxion_RenderView_UploadLighting(view, cmd);

    const FluxionRenderGraphBinding binding = { FLUXION_RENDER_VIEW_SHADOW_ATLAS_RESOURCE, atlas,
                                                Fluxion::Foundation::NoHandle<FluxionRHIBufferHandle>(),
                                                FLUXION_RHI_RESOURCE_STATE_SHADER_READ };

    FluxionRenderGraphInstantiateDesc instantiate{};
    instantiate.bindings = &binding;
    instantiate.bindingCount = 1;
    instantiate.context = Fluxion_Renderer_GetForwardOpaquePassUserData(renderer);

    FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(device);
    TEST_CHECK(ctx, Fluxion_RenderGraphAsset_Instantiate(&graphAsset, graph, &instantiate));

    Fluxion_Renderer_BeginFrame(renderer, view);
    const FluxionMat4 identity = Fluxion_Mat4_Identity();
    Fluxion_Renderer_DrawMesh(renderer, mesh, Fluxion::Foundation::NoHandle<FluxionMaterialHandle>(),
                              Fluxion::Foundation::NoHandle<FluxionRenderPipelineHandle>(), &identity);
    Fluxion_Renderer_UploadScene(renderer, cmd);

    TEST_CHECK(ctx, Fluxion_RenderGraph_Compile(graph));
    Fluxion_RenderGraph_Execute(graph, cmd);
    Fluxion_Renderer_EndFrame(renderer, cmd);

    const usize rowBytes = (usize)result.atlasSize * sizeof(f32);
    const usize alignedRow = (rowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) /
                             FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;

    FluxionRHIBufferDesc readbackDesc{ alignedRow * result.atlasSize, FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST,
                                       FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU, "RenderPipelineAssetGPU.Readback" };
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
        const u32 half = result.tileSize / 2;

        // Both ends of the same column, because one backend stores a
        // texture's rows from the bottom while the others store them
        // from the top -- see Test_ShadowPassGPU, where that is measured
        // rather than argued about. Whichever of the two is not the
        // clear is the one the tile landed in.
        f32 fromTop = 0.0f;
        f32 fromBottom = 0.0f;
        std::memcpy(&fromTop, mapped + (usize)half * alignedRow + (usize)half * sizeof(f32), sizeof(f32));
        std::memcpy(&fromBottom, mapped + (usize)(result.atlasSize - 1 - half) * alignedRow + (usize)half * sizeof(f32), sizeof(f32));

        result.centreDepth = fromTop < fromBottom ? fromTop : fromBottom;
        result.ran = true;

        Fluxion_RHI_UnmapBuffer(readback);
    }

    Fluxion_RenderGraph_Destroy(graph);
    Fluxion_RHI_DestroyBuffer(readback);
    Fluxion_RHI_DestroyFence(fence);
    Fluxion_RHI_DestroyCommandList(cmd);
    Fluxion_RenderView_Destroy(view);
    Fluxion_Renderer_Destroy(renderer);

    return result;
}

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
        FLUXION_LOG_WARN("RenderCoreTests", "No %s device -- pipeline assets were NOT checked on it.", backendName);
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RenderGraphPassRegistry_Init();

    const FluxionRenderGraphPassType nothingType = { "Test_PipelineNothingPass", NothingPassSetup, NothingPassExecute };
    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Register(&nothingType));

    FluxionMeshBufferDesc meshDesc{};
    meshDesc.vertexData = kQuad;
    meshDesc.vertexDataSize = sizeof(kQuad);
    meshDesc.indexData = kQuadIndices;
    meshDesc.indexDataSize = sizeof(kQuadIndices);
    meshDesc.use16BitIndices = true;
    meshDesc.vertexLayout.attributes[0].location = 0;
    meshDesc.vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    meshDesc.vertexLayout.attributes[0].offset = offsetof(PipelineVertex, position);
    meshDesc.vertexLayout.attributes[1].location = 1;
    meshDesc.vertexLayout.attributes[1].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    meshDesc.vertexLayout.attributes[1].offset = offsetof(PipelineVertex, normal);
    meshDesc.vertexLayout.attributes[2].location = 2;
    meshDesc.vertexLayout.attributes[2].format = FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT;
    meshDesc.vertexLayout.attributes[2].offset = offsetof(PipelineVertex, tangent);
    meshDesc.vertexLayout.attributes[3].location = 3;
    meshDesc.vertexLayout.attributes[3].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    meshDesc.vertexLayout.attributes[3].offset = offsetof(PipelineVertex, uv);
    meshDesc.vertexLayout.attributeCount = 4;
    meshDesc.vertexLayout.stride = sizeof(PipelineVertex);
    meshDesc.bounds = FluxionAABB{ FluxionVec3{ -4.0f, 0.0f, -4.0f }, FluxionVec3{ 4.0f, 0.0f, 4.0f } };
    meshDesc.debugName = "RenderPipelineAssetGPU.Quad";
    FluxionMeshBufferHandle mesh = Fluxion_MeshBuffer_Create(device, queue, &meshDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(mesh));

    const RunResult shadowed = DrawThroughPipeline(ctx, device, queue, mesh, kShadowedPipelineText, kShadowedGraphText);
    const RunResult shadowless = DrawThroughPipeline(ctx, device, queue, mesh, kShadowlessPipelineText, kShadowlessGraphText);

    TEST_CHECK(ctx, shadowed.ran && shadowless.ran);
    if (shadowed.ran && shadowless.ran)
    {
        FLUXION_LOG_INFO("RenderCoreTests",
                         "%s: with the shadow pipeline the atlas is %u texels and reads %.4f; without it, %u texels and %.4f.",
                         backendName, shadowed.atlasSize, (f64)shadowed.centreDepth, shadowless.atlasSize, (f64)shadowless.centreDepth);

        // The quad sits halfway along the light's slab, so a pipeline
        // that draws shadows lands it at half the depth range.
        TEST_CHECK(ctx, shadowed.centreDepth > 0.45f && shadowed.centreDepth < 0.55f);

        // And a pipeline whose graph has no shadow pass leaves the clear
        // where it was: nothing shadows anything, which is a lit scene
        // rather than a broken one.
        TEST_CHECK(ctx, shadowless.centreDepth > 0.99f);

        // The size came from the asset too, not from a constant.
        TEST_CHECK(ctx, shadowed.atlasSize > shadowless.atlasSize);
    }

    Fluxion_MeshBuffer_Destroy(mesh);
    Fluxion_RenderGraphPassRegistry_Shutdown();

    Fluxion_RHI_Device_CollectGarbage(device);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}

} // namespace

extern "C" void Test_RenderPipelineAssetGPU_Run(TestContext* ctx)
{
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No shader compiler on this machine -- pipeline assets were NOT checked on a device.");
        return;
    }

    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_OPENGL, "OpenGL");
}
