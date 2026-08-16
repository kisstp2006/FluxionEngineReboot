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

#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraph.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>
#include <Fluxion/RenderCore/Renderer/Material.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>
#include <Fluxion/RenderCore/Renderer/RenderPipeline.h>
#include <Fluxion/RenderCore/Renderer/RenderTarget.h>
#include <Fluxion/RenderCore/Renderer/RenderView.h>
#include <Fluxion/RenderCore/Renderer/Renderer.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>

#include <cstdio>

namespace
{

struct NullRHIFixture
{
    FluxionRHIInstanceHandle instance;
    FluxionRHIDeviceHandle device;
    FluxionRHIQueueHandle queue;
    FluxionRHICommandListHandle commandList;
};

NullRHIFixture CreateNullRHIFixture()
{
    NullRHIFixture fixture;
    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests", false };
    fixture.instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &instanceDesc);

    FluxionRHIAdapterHandle adapters[1];
    Fluxion_RHI_EnumerateAdapters(fixture.instance, adapters, 1);

    FluxionRHIDeviceDesc deviceDesc = { 0 };
    fixture.device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    fixture.queue = Fluxion_RHI_GetQueue(fixture.device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    fixture.commandList = Fluxion_RHI_CreateCommandList(fixture.device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    return fixture;
}

void DestroyNullRHIFixture(const NullRHIFixture& fixture)
{
    Fluxion_RHI_DestroyCommandList(fixture.commandList);
    Fluxion_RHI_DestroyDevice(fixture.device);
    Fluxion_RHI_DestroyInstance(fixture.instance);
}

const char* kVertexSource =
    "[Input] Vector3 position;\n"
    "[Output] Vector4 Position;\n"
    "void main() {\n"
    "  Position = Vector4(position, 1.0);\n"
    "}\n";

const char* kFragmentSource =
    "[Uniform(Material)] Vector3 tint;\n"
    "[Target(0)] Vector4 fragColor;\n"
    "void main() {\n"
    "  return Vector4(tint, 1.0);\n"
    "}\n";

struct Vertex
{
    f32 position[3];
};

} // namespace

// Full round trip: ShaderProgram + Material + MeshBuffer + RenderView +
// Renderer, one BeginFrame -> DrawMesh -> [compile + execute a render
// graph containing "ForwardOpaquePass"] -> EndFrame, asserting the
// registered pass's Execute actually ran and issued exactly one draw
// call for the one DrawPacket submitted (see
// Fluxion_Renderer_GetLastDrawCallCount's comment in Renderer.h for why
// this is the observable signal, mirroring Test_GraphCompile.cpp's
// ExecutionLog counter pattern where a made-up test pass type isn't an
// option here -- "ForwardOpaquePass" is a real, already-registered
// production pass type, not something this test defines itself).
extern "C" void Test_RendererFrame_Run(TestContext* ctx)
{
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        std::fprintf(stderr, "  SKIP: dxc not found on this machine -- skipping RendererFrame checks\n");
        return;
    }

    Fluxion_RenderGraphPassRegistry_Init();

    NullRHIFixture fixture = CreateNullRHIFixture();

    FluxionRHITextureDesc colorTextureDesc;
    colorTextureDesc.width = 64;
    colorTextureDesc.height = 64;
    colorTextureDesc.depth = 1;
    colorTextureDesc.mipLevels = 1;
    colorTextureDesc.arrayLayers = 1;
    colorTextureDesc.sampleCount = 1;
    colorTextureDesc.format = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    colorTextureDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET;
    colorTextureDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    colorTextureDesc.debugName = "Test_RendererFrame.Color";
    FluxionRHITextureHandle colorTexture = Fluxion_RHI_CreateTexture(fixture.device, &colorTextureDesc);

    FluxionRHITextureViewDesc colorViewDesc = { colorTexture, colorTextureDesc.format, 0, 1, 0, 1, FLUXION_RHI_TEXTURE_DIMENSION_2D };
    FluxionRHITextureViewHandle colorView = Fluxion_RHI_CreateTextureView(fixture.device, &colorViewDesc);

    FluxionRenderTargetDesc targetDesc;
    targetDesc.colorViews[0] = colorView;
    targetDesc.colorViewCount = 1;
    targetDesc.depthView = FluxionRHITextureViewHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRenderTargetHandle renderTarget = Fluxion_RenderTarget_Create(fixture.device, &targetDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(renderTarget));

    FluxionRenderViewDesc viewDesc{};
    viewDesc.viewMatrix = Fluxion_Mat4_Identity();
    viewDesc.projectionMatrix = Fluxion_Mat4_Identity();
    viewDesc.viewport = FluxionViewport{ 0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f };
    viewDesc.scissor = FluxionScissorRect{ 0, 0, 64, 64 };
    viewDesc.renderTarget = renderTarget;
    viewDesc.layerMask = 0xFFFFFFFFu;
    FluxionRenderViewHandle view = Fluxion_RenderView_Create(fixture.device, &viewDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(view));
    Fluxion_RenderView_UpdateFrameConstants(view);

    FluxionShaderProgramDesc programDesc{};
    programDesc.debugName = "Test_RendererFrame.Program";
    programDesc.vertexSource = kVertexSource;
    programDesc.fragmentSource = kFragmentSource;
    FluxionShaderProgramHandle program = Fluxion_ShaderProgram_Create(fixture.device, &programDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(program));

    FluxionMaterialHandle material = Fluxion_Material_Create(fixture.device, program);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(material));
    Fluxion_Material_SetVec3(material, "tint", FluxionVec3{ 1.0f, 0.0f, 0.0f });
    Fluxion_Material_FlushDirty(material);

    Vertex vertices[3] = {
        { { 0.0f, 0.5f, 0.0f } },
        { { 0.5f, -0.5f, 0.0f } },
        { { -0.5f, -0.5f, 0.0f } },
    };

    FluxionRHIVertexLayout vertexLayout{};
    vertexLayout.attributes[0].location = 0;
    vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    vertexLayout.attributes[0].offset = 0;
    vertexLayout.attributeCount = 1;
    vertexLayout.stride = sizeof(Vertex);

    FluxionMeshBufferDesc meshDesc{};
    meshDesc.vertexData = vertices;
    meshDesc.vertexDataSize = sizeof(vertices);
    meshDesc.vertexLayout = vertexLayout;
    meshDesc.bounds = FluxionAABB{ FluxionVec3{ -0.5f, -0.5f, 0.0f }, FluxionVec3{ 0.5f, 0.5f, 0.0f } };
    meshDesc.debugName = "Test_RendererFrame.Triangle";
    FluxionMeshBufferHandle mesh = Fluxion_MeshBuffer_Create(fixture.device, fixture.queue, &meshDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(mesh));

    FluxionRenderPipelineHandle pipeline = Fluxion_RenderPipeline_Create(fixture.device, program, FLUXION_RENDER_PIPELINE_CATEGORY_OPAQUE, FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, FLUXION_RHI_FORMAT_D32_FLOAT);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(pipeline));

    FluxionRendererHandle renderer = Fluxion_Renderer_Create(fixture.device, fixture.queue);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(renderer));

    // See ForwardOpaquePass.c's top-of-file comment: the caller imports
    // the render target's underlying texture(s) under this exact naming
    // convention before adding the pass node.
    FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(fixture.device);
    Fluxion_RenderGraph_ImportTexture(graph, "ForwardOpaquePass.Color0", colorTexture, FLUXION_RHI_RESOURCE_STATE_UNDEFINED);
    Fluxion_RenderGraph_AddPassFromRegistry(graph, "ForwardOpaquePass", Fluxion_Renderer_GetForwardOpaquePassUserData(renderer));

    Fluxion_Renderer_BeginFrame(renderer, view);
    Fluxion_Renderer_DrawMesh(renderer, mesh, material, pipeline, nullptr);

    TEST_CHECK(ctx, Fluxion_RenderGraph_Compile(graph));

    Fluxion_RHI_CommandList_Begin(fixture.commandList);
    Fluxion_RenderGraph_Execute(graph, fixture.commandList);
    Fluxion_RHI_CommandList_End(fixture.commandList);

    // The pass's Execute callback genuinely ran (not culled -- it writes
    // the imported "ForwardOpaquePass.Color0" resource) and issued two
    // draw calls: the one DrawPacket submitted above, and the sky behind
    // it.
    //
    // Two rather than one because a frame now has a sky in it. That is
    // not a number to relax -- it is the statement that the sky is drawn
    // exactly once per frame, and it would catch a sky drawn per packet
    // just as surely as it caught the change that put it here.
    TEST_CHECK(ctx, Fluxion_Renderer_GetLastDrawCallCount(renderer) == 2);

    Fluxion_Renderer_EndFrame(renderer, fixture.commandList);

    Fluxion_RenderGraph_Destroy(graph);
    Fluxion_Renderer_Destroy(renderer);
    Fluxion_RenderPipeline_Destroy(pipeline);
    Fluxion_MeshBuffer_Destroy(mesh);
    Fluxion_Material_Destroy(material);
    Fluxion_ShaderProgram_Destroy(program);
    Fluxion_RenderView_Destroy(view);
    Fluxion_RenderTarget_Destroy(renderTarget);
    Fluxion_RHI_DestroyTextureView(colorView);
    Fluxion_RHI_DestroyTexture(colorTexture);

    DestroyNullRHIFixture(fixture);

    Fluxion_RenderGraphPassRegistry_Shutdown();
}
