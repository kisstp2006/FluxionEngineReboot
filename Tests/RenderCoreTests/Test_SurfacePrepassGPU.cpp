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
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>
#include <Fluxion/RenderCore/Renderer/Material.h>
#include <Fluxion/RenderCore/Renderer/MaterialParameters.h>
#include <Fluxion/RenderCore/Renderer/MaterialShader.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>
#include <Fluxion/RenderCore/Renderer/RenderPipeline.h>
#include <Fluxion/RenderCore/Renderer/RenderTarget.h>
#include <Fluxion/RenderCore/Renderer/Renderer.h>
#include <Fluxion/RenderCore/Renderer/RenderView.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>
#include <Fluxion/RenderCore/Renderer/TextureDefaults.h>
#include <Fluxion/RenderCore/Scene/RenderWorld.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

// WHAT THE FRAME RECORDED ABOUT ITSELF, READ BACK AND DECODED.
//
// Three later effects are built on this one texture, and none of them can
// tell a wrong answer from a right one -- occlusion given a normal that
// points the other way simply darkens the wrong side of everything, and
// reflections given a roughness of zero make a mirror out of a wall. So
// what is checked here is the texture itself, before anything reads it.
//
// The two quads are the point. They face DIFFERENT WAYS and have
// DIFFERENT roughness, so every check below is a comparison rather than a
// value: a pass that wrote a constant, or that recorded one surface for
// the whole frame, fails on the difference even where it happens to be
// close on the number.

extern "C" void FluxionForwardOpaquePass_Execute(FluxionRHICommandListHandle commandList, void* userData);

namespace
{

constexpr u32 kWidth = 64;
constexpr u32 kHeight = 64;

// DELIBERATELY DIFFERENT, and far enough apart that eight bits cannot
// blur them together.
constexpr f32 kLeftRoughness = 0.20f;
constexpr f32 kRightRoughness = 0.85f;

struct PrepassVertex
{
    f32 position[3];
    f32 normal[3];
    f32 tangent[4];
    f32 uv[2];
};

const char* const kMaterialSource = R"(
#include "Fluxion/Material.jsl"

SurfaceData EvaluateSurface() {
  return StandardSurface();
}
)";

FluxionMat4 Orthographic()
{
    FluxionMat4 m = Fluxion_Mat4_Identity();
    m.m[0][0] = 1.0f;
    m.m[1][1] = 1.0f;
    m.m[2][2] = -0.25f;
    m.m[2][3] = 0.5f;
    return m;
}

FluxionMat4 TranslationOf(f32 x, f32 y, f32 z)
{
    FluxionMat4 m = Fluxion_Mat4_Identity();
    m.m[0][3] = x;
    m.m[1][3] = y;
    m.m[2][3] = z;
    return m;
}

// The left half of the frame, facing straight out of it.
const PrepassVertex kFacingQuad[4] = {
    { { -0.9f, -0.9f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
    { { -0.1f, -0.9f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
    { { -0.1f, 0.9f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
    { { -0.9f, 0.9f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
};

// The right half, tilted so that it faces up as much as it faces out.
// The direction is normalized here rather than left to the shader,
// because what this test compares against is this exact vector.
const PrepassVertex kTiltedQuad[4] = {
    { { 0.1f, -0.9f, 0.0f }, { 0.0f, 0.7071f, 0.7071f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
    { { 0.9f, -0.9f, 0.0f }, { 0.0f, 0.7071f, 0.7071f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
    { { 0.9f, 0.9f, 0.0f }, { 0.0f, 0.7071f, 0.7071f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
    { { 0.1f, 0.9f, 0.0f }, { 0.0f, 0.7071f, 0.7071f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
};

const u16 kQuadIndices[6] = { 0, 1, 2, 0, 2, 3 };

// The other end of what the pass wrote -- the same mapping as
// Surface.jsl's DecodeNormalOctahedral, on the processor, so that a
// change to one and not the other shows up here rather than in a picture.
void DecodeNormal(f32 x, f32 y, f32* outX, f32* outY, f32* outZ)
{
    const f32 foldedX = x * 2.0f - 1.0f;
    const f32 foldedY = y * 2.0f - 1.0f;

    f32 nx = foldedX;
    f32 ny = foldedY;
    const f32 nz = 1.0f - std::fabs(foldedX) - std::fabs(foldedY);
    if (nz < 0.0f)
    {
        nx = (1.0f - std::fabs(foldedY)) * (foldedX >= 0.0f ? 1.0f : -1.0f);
        ny = (1.0f - std::fabs(foldedX)) * (foldedY >= 0.0f ? 1.0f : -1.0f);
    }

    const f32 length = std::sqrt(nx * nx + ny * ny + nz * nz);
    *outX = length > 0.0f ? nx / length : 0.0f;
    *outY = length > 0.0f ? ny / length : 0.0f;
    *outZ = length > 0.0f ? nz / length : 0.0f;
}

f32 Channel(const std::vector<u8>& pixels, u32 x, u32 y, u32 channel)
{
    const usize at = ((usize)y * kWidth + x) * 4u + channel;
    if (at >= pixels.size()) return 0.0f;
    return (f32)pixels[at] / 255.0f;
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
        FLUXION_LOG_WARN("RenderCoreTests", "No %s device -- the recorded surfaces were NOT measured on it.", backendName);
        Fluxion_RHI_DestroyInstance(instance);
        return;
    }

    FluxionRHIQueueHandle queue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    FluxionRHICommandListHandle cmd = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RenderGraphPassRegistry_Init();

    const FluxionRHIFormat outputFormat = FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT;

    FluxionRHITextureDesc colorDesc{};
    colorDesc.width = kWidth;
    colorDesc.height = kHeight;
    colorDesc.depth = 1;
    colorDesc.mipLevels = 1;
    colorDesc.arrayLayers = 1;
    colorDesc.sampleCount = 1;
    colorDesc.format = outputFormat;
    colorDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET | FLUXION_RHI_TEXTURE_USAGE_TRANSFER_SRC;
    colorDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    colorDesc.debugName = "SurfacePrepassGPU.Color";
    FluxionRHITextureHandle color = Fluxion_RHI_CreateTexture(device, &colorDesc);

    FluxionRHITextureDesc depthDesc = colorDesc;
    depthDesc.format = FLUXION_RHI_FORMAT_D32_FLOAT;
    depthDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL;
    depthDesc.debugName = "SurfacePrepassGPU.Depth";
    FluxionRHITextureHandle depth = Fluxion_RHI_CreateTexture(device, &depthDesc);

    FluxionRHITextureViewDesc viewDesc{};
    viewDesc.texture = color;
    viewDesc.format = colorDesc.format;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    FluxionRHITextureViewHandle colorView = Fluxion_RHI_CreateTextureView(device, &viewDesc);

    viewDesc.texture = depth;
    viewDesc.format = depthDesc.format;
    FluxionRHITextureViewHandle depthView = Fluxion_RHI_CreateTextureView(device, &viewDesc);

    FluxionRenderTargetDesc targetDesc{};
    targetDesc.colorViews[0] = colorView;
    targetDesc.colorViewCount = 1;
    targetDesc.depthView = depthView;
    FluxionRenderTargetHandle target = Fluxion_RenderTarget_Create(device, &targetDesc);

    auto makeMesh = [&](const PrepassVertex* vertices, const char* name) {
        FluxionMeshBufferDesc meshDesc{};
        std::memset(&meshDesc, 0, sizeof(meshDesc));
        meshDesc.vertexData = vertices;
        meshDesc.vertexDataSize = sizeof(PrepassVertex) * 4u;
        meshDesc.indexData = kQuadIndices;
        meshDesc.indexDataSize = sizeof(kQuadIndices);
        meshDesc.use16BitIndices = true;
        meshDesc.vertexLayout.attributes[0].location = 0;
        meshDesc.vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
        meshDesc.vertexLayout.attributes[0].offset = offsetof(PrepassVertex, position);
        meshDesc.vertexLayout.attributes[1].location = 1;
        meshDesc.vertexLayout.attributes[1].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
        meshDesc.vertexLayout.attributes[1].offset = offsetof(PrepassVertex, normal);
        meshDesc.vertexLayout.attributes[2].location = 2;
        meshDesc.vertexLayout.attributes[2].format = FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT;
        meshDesc.vertexLayout.attributes[2].offset = offsetof(PrepassVertex, tangent);
        meshDesc.vertexLayout.attributes[3].location = 3;
        meshDesc.vertexLayout.attributes[3].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
        meshDesc.vertexLayout.attributes[3].offset = offsetof(PrepassVertex, uv);
        meshDesc.vertexLayout.attributeCount = 4;
        meshDesc.vertexLayout.stride = sizeof(PrepassVertex);
        meshDesc.bounds = FluxionAABB{ FluxionVec3{ -1.0f, -1.0f, -1.0f }, FluxionVec3{ 1.0f, 1.0f, 1.0f } };
        meshDesc.debugName = name;
        return Fluxion_MeshBuffer_Create(device, queue, &meshDesc);
    };

    FluxionMeshBufferHandle facingMesh = makeMesh(kFacingQuad, "SurfacePrepassGPU.Facing");
    FluxionMeshBufferHandle tiltedMesh = makeMesh(kTiltedQuad, "SurfacePrepassGPU.Tilted");

    char* vertexSource = Fluxion_MaterialShader_BuildVertexSource(FLUXION_MATERIAL_PASS_FORWARD);
    char* fragmentSource = Fluxion_MaterialShader_BuildFragmentSource(kMaterialSource, FLUXION_MATERIAL_PASS_FORWARD);

    FluxionShaderProgramDesc programDesc{};
    programDesc.debugName = "SurfacePrepassGPU.Standard";
    programDesc.vertexSource = vertexSource;
    programDesc.fragmentSource = fragmentSource;
    FluxionShaderProgramHandle program = Fluxion_ShaderProgram_Create(device, &programDesc);
    Fluxion_MaterialShader_FreeSource(vertexSource);
    Fluxion_MaterialShader_FreeSource(fragmentSource);

    // THE SAME SOURCE, THE OTHER ENTRY POINT -- which is the whole claim
    // the prepass makes about itself.
    char* prepassVertexSource = Fluxion_MaterialShader_BuildVertexSource(FLUXION_MATERIAL_PASS_NORMAL_ROUGHNESS);
    char* prepassFragmentSource = Fluxion_MaterialShader_BuildFragmentSource(kMaterialSource, FLUXION_MATERIAL_PASS_NORMAL_ROUGHNESS);

    FluxionShaderProgramDesc prepassProgramDesc{};
    prepassProgramDesc.debugName = "SurfacePrepassGPU.Surface";
    prepassProgramDesc.vertexSource = prepassVertexSource;
    prepassProgramDesc.fragmentSource = prepassFragmentSource;
    FluxionShaderProgramHandle prepassProgram = Fluxion_ShaderProgram_Create(device, &prepassProgramDesc);
    Fluxion_MaterialShader_FreeSource(prepassVertexSource);
    Fluxion_MaterialShader_FreeSource(prepassFragmentSource);

    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(prepassProgram));

    Fluxion_TextureDefaults_Init(device, queue);

    FluxionRHISamplerDesc samplerDesc{};
    std::memset(&samplerDesc, 0, sizeof(samplerDesc));
    samplerDesc.minFilter = FLUXION_RHI_FILTER_LINEAR;
    samplerDesc.magFilter = FLUXION_RHI_FILTER_LINEAR;
    samplerDesc.mipFilter = FLUXION_RHI_FILTER_NEAREST;
    samplerDesc.addressModeU = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeV = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeW = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.maxAnisotropy = 1.0f;
    samplerDesc.debugName = "SurfacePrepassGPU.Sampler";
    FluxionRHISamplerHandle sampler = Fluxion_RHI_CreateSampler(device, &samplerDesc);

    auto makeMaterial = [&](f32 roughness) {
        FluxionMaterialHandle material = Fluxion_Material_Create(device, program);
        Fluxion_Material_SetBaseColor(material, FluxionVec4{ 1.0f, 1.0f, 1.0f, 1.0f });
        Fluxion_Material_SetRoughness(material, roughness);
        Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_BASE_COLOR, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
        Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_METALLIC_ROUGHNESS, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
        Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_NORMAL, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_FLAT_NORMAL), sampler);
        Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_OCCLUSION, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
        Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_EMISSIVE, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
        Fluxion_Material_FlushDirty(material);
        return material;
    };

    FluxionMaterialHandle leftMaterial = makeMaterial(kLeftRoughness);
    FluxionMaterialHandle rightMaterial = makeMaterial(kRightRoughness);

    FluxionRendererHandle renderer = Fluxion_Renderer_Create(device, queue);
    Fluxion_Renderer_SetOutputColorFormat(renderer, outputFormat);
    Fluxion_Renderer_SetPostProcessEnabled(renderer, false);
    Fluxion_Renderer_SetSurfacePrepassEnabled(renderer, true);

    // Built against the target directly, because the chain is off: what
    // this test reads is the prepass texture, not the picture.
    FluxionRenderPipelineHandle pipeline = Fluxion_RenderPipeline_Create(device, program, FLUXION_RENDER_PIPELINE_CATEGORY_OPAQUE,
                                                                        outputFormat, depthDesc.format);
    Fluxion_RenderPipeline_SetPrepassProgram(pipeline, prepassProgram);
    TEST_CHECK(ctx, Fluxion_RenderPipeline_HasPrepassProgram(pipeline));

    const usize rowBytes = (usize)kWidth * 4u;
    const usize alignedRowBytes = (rowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) /
                                  FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;
    FluxionRHIBufferDesc readbackDesc{ alignedRowBytes * kHeight, FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST,
                                       FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU, "SurfacePrepassGPU.Readback" };
    FluxionRHIBufferHandle readback = Fluxion_RHI_CreateBuffer(device, &readbackDesc);

    FluxionRenderViewDesc viewSettings{};
    std::memset(&viewSettings, 0, sizeof(viewSettings));
    viewSettings.viewMatrix = TranslationOf(0.0f, 0.0f, -1.0f);
    viewSettings.projectionMatrix = Orthographic();
    viewSettings.viewport = FluxionViewport{ 0.0f, 0.0f, (f32)kWidth, (f32)kHeight, 0.0f, 1.0f };
    viewSettings.scissor = FluxionScissorRect{ 0, 0, kWidth, kHeight };
    viewSettings.renderTarget = target;
    viewSettings.layerMask = 0xFFFFFFFFu;
    viewSettings.exposure = 1.0f;

    FluxionRenderViewHandle view = Fluxion_RenderView_Create(device, &viewSettings);
    Fluxion_RenderView_UpdateFrameConstants(view);
    Fluxion_Renderer_BeginFrame(renderer, view);

    FluxionRenderWorld world{};
    std::vector<u8> pixels;
    bool read = false;

    if (Fluxion_RenderWorld_Init(&world))
    {
        auto addObject = [&](FluxionMeshBufferHandle mesh, FluxionMaterialHandle material) {
            FluxionRenderObject object{};
            std::memset(&object, 0, sizeof(object));
            object.transform = Fluxion_Mat4_Identity();
            object.previousTransform = object.transform;
            object.mesh = mesh;
            object.material = material;
            object.pipeline = pipeline;
            object.layerMask = 0xFFFFFFFFu;
            object.visible = true;
            Fluxion_RenderWorld_AddObject(&world, &object);
        };
        addObject(facingMesh, leftMaterial);
        addObject(tiltedMesh, rightMaterial);

        Fluxion_RHI_CommandList_Begin(cmd);
        Fluxion_Renderer_UpdateEnvironment(renderer, view, cmd);
        Fluxion_Renderer_SubmitRenderWorld(renderer, &world);
        Fluxion_Renderer_UploadScene(renderer, cmd);

        const FluxionRHIBufferHandle noBuffer = Fluxion::Foundation::NoHandle<FluxionRHIBufferHandle>();

        FluxionRHIBarrier atlasToRead = { Fluxion_RenderView_GetShadowAtlasTexture(view), noBuffer,
                                          FLUXION_RHI_RESOURCE_STATE_UNDEFINED, FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
        Fluxion_RHI_CommandList_Barrier(cmd, &atlasToRead, 1);

        FluxionRHIBarrier depthToWrite = { depth, noBuffer, FLUXION_RHI_RESOURCE_STATE_UNDEFINED,
                                           FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE, 0, 0 };
        Fluxion_RHI_CommandList_Barrier(cmd, &depthToWrite, 1);

        FluxionRHIBarrier colorToTarget = { color, noBuffer, FLUXION_RHI_RESOURCE_STATE_UNDEFINED,
                                            FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, 0, 0 };
        Fluxion_RHI_CommandList_Barrier(cmd, &colorToTarget, 1);

        // The prepass runs inside this, before anything is lit.
        FluxionForwardOpaquePass_Execute(cmd, Fluxion_Renderer_GetForwardOpaquePassUserData(renderer));

        const FluxionRHITextureHandle recorded = Fluxion_Renderer_GetNormalRoughnessTexture(renderer);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(recorded));

        if (FLUXION_HANDLE_IS_VALID(recorded))
        {
            FluxionRHIBarrier toSource = { recorded, noBuffer, FLUXION_RHI_RESOURCE_STATE_SHADER_READ,
                                           FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE, 0, 0 };
            Fluxion_RHI_CommandList_Barrier(cmd, &toSource, 1);
            Fluxion_RHI_CommandList_CopyTextureToBuffer(cmd, recorded, 0, 0, readback, 0);
        }

        Fluxion_Renderer_EndFrame(renderer, cmd);
        Fluxion_RHI_CommandList_End(cmd);

        FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(device, false);
        Fluxion_RHI_Queue_Submit(queue, &cmd, 1, fence);
        const bool finished = Fluxion_RHI_WaitForFence(fence);
        Fluxion_RHI_DestroyFence(fence);

        if (finished)
        {
            if (const u8* mapped = (const u8*)Fluxion_RHI_MapBuffer(readback))
            {
                pixels.assign((usize)kWidth * kHeight * 4u, 0);
                for (u32 y = 0; y < kHeight; ++y)
                {
                    std::memcpy(&pixels[(usize)y * kWidth * 4u], mapped + (usize)y * alignedRowBytes, rowBytes);
                }
                Fluxion_RHI_UnmapBuffer(readback);
                read = true;
            }
        }

        Fluxion_RenderWorld_Shutdown(&world);
    }

    TEST_CHECK(ctx, read);

    if (read)
    {
        // A COLUMN IN EACH QUAD, and a row where neither of them is. The
        // quads split the frame down the middle and stop short of the
        // top and bottom, so these three are inside the left one, inside
        // the right one, and outside both -- whichever way round this
        // backend stores its rows.
        const u32 leftColumn = kWidth / 4u;
        const u32 rightColumn = (kWidth * 3u) / 4u;
        const u32 middleRow = kHeight / 2u;

        f32 leftX = 0.0f;
        f32 leftY = 0.0f;
        f32 leftZ = 0.0f;
        DecodeNormal(Channel(pixels, leftColumn, middleRow, 0), Channel(pixels, leftColumn, middleRow, 1), &leftX, &leftY, &leftZ);

        f32 rightX = 0.0f;
        f32 rightY = 0.0f;
        f32 rightZ = 0.0f;
        DecodeNormal(Channel(pixels, rightColumn, middleRow, 0), Channel(pixels, rightColumn, middleRow, 1), &rightX, &rightY, &rightZ);

        const f32 leftRoughness = Channel(pixels, leftColumn, middleRow, 2);
        const f32 rightRoughness = Channel(pixels, rightColumn, middleRow, 2);
        const f32 leftWritten = Channel(pixels, leftColumn, middleRow, 3);
        const f32 rightWritten = Channel(pixels, rightColumn, middleRow, 3);

        FLUXION_LOG_INFO("RenderCoreTests",
                         "%s: the facing quad decodes to %.3f %.3f %.3f at roughness %.3f, the tilted one to "
                         "%.3f %.3f %.3f at roughness %.3f; both are marked written (%.0f, %.0f).",
                         backendName, (f64)leftX, (f64)leftY, (f64)leftZ, (f64)leftRoughness, (f64)rightX, (f64)rightY,
                         (f64)rightZ, (f64)rightRoughness, (f64)leftWritten, (f64)rightWritten);

        // SOMETHING WAS RECORDED AT ALL. Everything below reads these two
        // pixels, and all of it would pass on a cleared texture that
        // happened to decode to something plausible.
        TEST_CHECK(ctx, leftWritten > 0.5f);
        TEST_CHECK(ctx, rightWritten > 0.5f);

        // THE ONE FACING THE CAMERA FACES THE CAMERA. Eight bits of
        // octahedral is worth about a quarter of a degree, so a hundredth
        // is far outside what quantisation can explain.
        TEST_CHECK(ctx, std::fabs(leftZ - 1.0f) < 0.01f);
        TEST_CHECK(ctx, std::fabs(leftY) < 0.01f);

        // AND THE TILTED ONE IS TILTED, by the amount it was built with:
        // equal parts up and out.
        TEST_CHECK(ctx, std::fabs(rightY - 0.7071f) < 0.02f);
        TEST_CHECK(ctx, std::fabs(rightZ - 0.7071f) < 0.02f);

        // WHICH IS THE HALF THAT MATTERS: the two are different. A pass
        // that recorded one surface for the whole frame would have put
        // the same direction in both.
        TEST_CHECK(ctx, std::fabs(rightY - leftY) > 0.5f);

        // The roughness each material was set to, within what eight bits
        // can hold -- and, again, different from each other.
        TEST_CHECK(ctx, std::fabs(leftRoughness - kLeftRoughness) < 0.02f);
        TEST_CHECK(ctx, std::fabs(rightRoughness - kRightRoughness) < 0.02f);
        TEST_CHECK(ctx, rightRoughness > leftRoughness + 0.5f);
    }

    Fluxion_RenderView_Destroy(view);
    Fluxion_Renderer_Destroy(renderer);
    Fluxion_Material_Destroy(leftMaterial);
    Fluxion_Material_Destroy(rightMaterial);
    Fluxion_RHI_DestroySampler(sampler);
    Fluxion_TextureDefaults_Shutdown();
    Fluxion_RenderPipeline_Destroy(pipeline);
    if (FLUXION_HANDLE_IS_VALID(prepassProgram)) Fluxion_ShaderProgram_Destroy(prepassProgram);
    Fluxion_ShaderProgram_Destroy(program);
    Fluxion_MeshBuffer_Destroy(facingMesh);
    Fluxion_MeshBuffer_Destroy(tiltedMesh);
    Fluxion_RenderTarget_Destroy(target);
    Fluxion_RHI_DestroyBuffer(readback);
    Fluxion_RHI_DestroyTextureView(colorView);
    Fluxion_RHI_DestroyTextureView(depthView);
    Fluxion_RHI_DestroyTexture(color);
    Fluxion_RHI_DestroyTexture(depth);
    Fluxion_RenderGraphPassRegistry_Shutdown();
    Fluxion_RHI_DestroyCommandList(cmd);
    Fluxion_RHI_Device_CollectGarbage(device);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}

} // namespace

extern "C" void Test_SurfacePrepassGPU_Run(TestContext* ctx)
{
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No shader compiler on this machine -- the recorded surfaces were NOT measured here.");
        return;
    }

    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_OPENGL, "OpenGL");
}
