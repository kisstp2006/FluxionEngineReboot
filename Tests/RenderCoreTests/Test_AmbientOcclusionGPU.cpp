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

// A CORNER IS DARKER THAN THE OPEN FLOOR, AND A LAMP IS NOT.
//
// Two quads meeting at a right angle, lit by nothing but a flat ambient
// and their own emission. Every check below is a comparison between two
// frames of that scene, one with the occlusion measured and one without.
//
// THE THIRD CHECK IS THE ONE WORTH WRITING THE TEST FOR. Occlusion is a
// statement about INDIRECT light: how much of the surroundings a point
// can see. It has no business touching light a surface emits, or light
// arriving from a lamp -- and the easy way to implement it, multiplying
// the finished picture, does exactly that. So the material here emits a
// known amount, and the crease is checked to be no darker than that
// emission however much of its sky is taken away. A pass that darkened
// the picture rather than the sky fails there and nowhere else.

extern "C" void FluxionForwardOpaquePass_Execute(FluxionRHICommandListHandle commandList, void* userData);

namespace
{

constexpr u32 kWidth = 96;
constexpr u32 kHeight = 96;

// What the surfaces send out on their own, and what arrives from the
// flat sky. Both known, so that the two can be told apart in the answer.
constexpr f32 kEmissive = 0.10f;
constexpr f32 kAmbient = 0.60f;

struct OcclusionVertex
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

f32 HalfBitsToFloat(u16 half)
{
    const u32 sign = (u32)(half >> 15) & 1u;
    const u32 exponent = (u32)(half >> 10) & 0x1Fu;
    const u32 mantissa = (u32)half & 0x3FFu;

    u32 bits;
    if (exponent == 0)
    {
        if (mantissa == 0)
        {
            bits = sign << 31;
        }
        else
        {
            u32 shifted = mantissa;
            i32 e = -1;
            while ((shifted & 0x400u) == 0) { shifted <<= 1; --e; }
            shifted &= 0x3FFu;
            bits = (sign << 31) | ((u32)(e + 15 + 112) << 23) | (shifted << 13);
        }
    }
    else if (exponent == 31)
    {
        bits = (sign << 31) | 0x7F800000u | (mantissa << 13);
    }
    else
    {
        bits = (sign << 31) | ((exponent + 112u) << 23) | (mantissa << 13);
    }

    f32 value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// THE SAME SHAPE THE ENGINE'S OWN SHADOW MATRICES USE, because the pass
// under test reads two of its terms straight out of the matrix to turn a
// depth back into a distance. A projection of a different shape would be
// asking it a question it does not claim to answer.
FluxionMat4 TranslationOf(f32 x, f32 y, f32 z)
{
    FluxionMat4 m = Fluxion_Mat4_Identity();
    m.m[0][3] = x;
    m.m[1][3] = y;
    m.m[2][3] = z;
    return m;
}

FluxionMat4 Perspective(f32 fovYRadians, f32 nearPlane, f32 farPlane)
{
    FluxionMat4 projection;
    std::memset(&projection, 0, sizeof(projection));

    const f32 focal = 1.0f / std::tan(fovYRadians * 0.5f);
    projection.m[0][0] = focal;
    projection.m[1][1] = focal;
    projection.m[2][2] = farPlane / (nearPlane - farPlane);
    projection.m[2][3] = (farPlane * nearPlane) / (nearPlane - farPlane);
    projection.m[3][2] = -1.0f;
    return projection;
}

// The floor the camera looks along, and the wall it runs into. The crease
// between them is the whole subject of this test.
const OcclusionVertex kFloorQuad[4] = {
    { { -3.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
    { { 3.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
    { { 3.0f, -1.0f, -6.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
    { { -3.0f, -1.0f, -6.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
};

const OcclusionVertex kWallQuad[4] = {
    { { -3.0f, -1.0f, -4.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
    { { 3.0f, -1.0f, -4.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
    { { 3.0f, 2.0f, -4.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
    { { -3.0f, 2.0f, -4.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
};

const u16 kQuadIndices[6] = { 0, 1, 2, 0, 2, 3 };

f32 Brightness(const std::vector<f32>& pixels, u32 x, u32 y)
{
    const usize at = ((usize)y * kWidth + x) * 4u;
    if (at + 2 >= pixels.size()) return 0.0f;
    return (pixels[at] + pixels[at + 1] + pixels[at + 2]) / 3.0f;
}

f32 Occlusion(const std::vector<u8>& pixels, u32 x, u32 y)
{
    const usize at = ((usize)y * kWidth + x) * 4u;
    if (at >= pixels.size()) return 1.0f;
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
        FLUXION_LOG_WARN("RenderCoreTests", "No %s device -- the occlusion was NOT measured on it.", backendName);
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
    colorDesc.debugName = "AmbientOcclusionGPU.Color";
    FluxionRHITextureHandle color = Fluxion_RHI_CreateTexture(device, &colorDesc);

    FluxionRHITextureDesc depthDesc = colorDesc;
    depthDesc.format = FLUXION_RHI_FORMAT_D32_FLOAT;
    depthDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL | FLUXION_RHI_TEXTURE_USAGE_SAMPLED;
    depthDesc.debugName = "AmbientOcclusionGPU.Depth";
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

    auto makeMesh = [&](const OcclusionVertex* vertices, const char* name) {
        FluxionMeshBufferDesc meshDesc{};
        std::memset(&meshDesc, 0, sizeof(meshDesc));
        meshDesc.vertexData = vertices;
        meshDesc.vertexDataSize = sizeof(OcclusionVertex) * 4u;
        meshDesc.indexData = kQuadIndices;
        meshDesc.indexDataSize = sizeof(kQuadIndices);
        meshDesc.use16BitIndices = true;
        meshDesc.vertexLayout.attributes[0].location = 0;
        meshDesc.vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
        meshDesc.vertexLayout.attributes[0].offset = offsetof(OcclusionVertex, position);
        meshDesc.vertexLayout.attributes[1].location = 1;
        meshDesc.vertexLayout.attributes[1].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
        meshDesc.vertexLayout.attributes[1].offset = offsetof(OcclusionVertex, normal);
        meshDesc.vertexLayout.attributes[2].location = 2;
        meshDesc.vertexLayout.attributes[2].format = FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT;
        meshDesc.vertexLayout.attributes[2].offset = offsetof(OcclusionVertex, tangent);
        meshDesc.vertexLayout.attributes[3].location = 3;
        meshDesc.vertexLayout.attributes[3].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
        meshDesc.vertexLayout.attributes[3].offset = offsetof(OcclusionVertex, uv);
        meshDesc.vertexLayout.attributeCount = 4;
        meshDesc.vertexLayout.stride = sizeof(OcclusionVertex);
        meshDesc.bounds = FluxionAABB{ FluxionVec3{ -3.0f, -1.0f, -6.0f }, FluxionVec3{ 3.0f, 2.0f, -1.0f } };
        meshDesc.debugName = name;
        return Fluxion_MeshBuffer_Create(device, queue, &meshDesc);
    };

    FluxionMeshBufferHandle floorMesh = makeMesh(kFloorQuad, "AmbientOcclusionGPU.Floor");
    FluxionMeshBufferHandle wallMesh = makeMesh(kWallQuad, "AmbientOcclusionGPU.Wall");

    char* vertexSource = Fluxion_MaterialShader_BuildVertexSource(FLUXION_MATERIAL_PASS_FORWARD);
    char* fragmentSource = Fluxion_MaterialShader_BuildFragmentSource(kMaterialSource, FLUXION_MATERIAL_PASS_FORWARD);
    FluxionShaderProgramDesc programDesc{};
    programDesc.debugName = "AmbientOcclusionGPU.Standard";
    programDesc.vertexSource = vertexSource;
    programDesc.fragmentSource = fragmentSource;
    FluxionShaderProgramHandle program = Fluxion_ShaderProgram_Create(device, &programDesc);
    Fluxion_MaterialShader_FreeSource(vertexSource);
    Fluxion_MaterialShader_FreeSource(fragmentSource);

    char* prepassVertex = Fluxion_MaterialShader_BuildVertexSource(FLUXION_MATERIAL_PASS_NORMAL_ROUGHNESS);
    char* prepassFragment = Fluxion_MaterialShader_BuildFragmentSource(kMaterialSource, FLUXION_MATERIAL_PASS_NORMAL_ROUGHNESS);
    FluxionShaderProgramDesc prepassDesc{};
    prepassDesc.debugName = "AmbientOcclusionGPU.Surface";
    prepassDesc.vertexSource = prepassVertex;
    prepassDesc.fragmentSource = prepassFragment;
    FluxionShaderProgramHandle prepassProgram = Fluxion_ShaderProgram_Create(device, &prepassDesc);
    Fluxion_MaterialShader_FreeSource(prepassVertex);
    Fluxion_MaterialShader_FreeSource(prepassFragment);

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
    samplerDesc.debugName = "AmbientOcclusionGPU.Sampler";
    FluxionRHISamplerHandle sampler = Fluxion_RHI_CreateSampler(device, &samplerDesc);

    FluxionMaterialHandle material = Fluxion_Material_Create(device, program);
    Fluxion_Material_SetBaseColor(material, FluxionVec4{ 1.0f, 1.0f, 1.0f, 1.0f });
    Fluxion_Material_SetEmissive(material, FluxionVec3{ kEmissive, kEmissive, kEmissive });
    Fluxion_Material_SetRoughness(material, 1.0f);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_BASE_COLOR, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_METALLIC_ROUGHNESS, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_NORMAL, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_FLAT_NORMAL), sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_OCCLUSION, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_EMISSIVE, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), sampler);
    Fluxion_Material_FlushDirty(material);

    FluxionRendererHandle renderer = Fluxion_Renderer_Create(device, queue);
    Fluxion_Renderer_SetOutputColorFormat(renderer, outputFormat);
    Fluxion_Renderer_SetPostProcessEnabled(renderer, false);
    Fluxion_Renderer_SetSurfacePrepassEnabled(renderer, true);

    FluxionRenderPipelineHandle pipeline = Fluxion_RenderPipeline_Create(device, program, FLUXION_RENDER_PIPELINE_CATEGORY_OPAQUE,
                                                                        outputFormat, depthDesc.format);
    Fluxion_RenderPipeline_SetPrepassProgram(pipeline, prepassProgram);

    const usize colorRowBytes = (usize)kWidth * 4u * sizeof(u16);
    const usize colorAligned = (colorRowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) /
                               FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;
    FluxionRHIBufferDesc colorReadbackDesc{ colorAligned * kHeight, FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST,
                                            FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU, "AmbientOcclusionGPU.ColorReadback" };
    FluxionRHIBufferHandle colorReadback = Fluxion_RHI_CreateBuffer(device, &colorReadbackDesc);

    const usize occlusionRowBytes = (usize)kWidth * 4u;
    const usize occlusionAligned = (occlusionRowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) /
                                   FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;
    FluxionRHIBufferDesc occlusionReadbackDesc{ occlusionAligned * kHeight, FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST,
                                                FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU, "AmbientOcclusionGPU.OcclusionReadback" };
    FluxionRHIBufferHandle occlusionReadback = Fluxion_RHI_CreateBuffer(device, &occlusionReadbackDesc);

    std::vector<f32> withoutOcclusion;
    std::vector<f32> withOcclusion;
    std::vector<u8> occlusionMap;

    auto renderOnce = [&](f32 eyeHeight, bool recordSurfaces, bool occlude, bool first, std::vector<f32>& outColor,
                          std::vector<u8>* outOcclusion) {
        Fluxion_Renderer_SetSurfacePrepassEnabled(renderer, recordSurfaces);
        Fluxion_Renderer_SetAmbientOcclusionEnabled(renderer, occlude);

        FluxionRenderViewDesc viewSettings{};
        std::memset(&viewSettings, 0, sizeof(viewSettings));
        // A view matrix moves the WORLD, so lowering the eye raises
        // everything else. At an eye height near the floor the surface is
        // seen almost edge-on, which is the case the denoise after the
        // search has the most trouble with -- see the check that uses it.
        viewSettings.viewMatrix = TranslationOf(0.0f, -eyeHeight, 0.0f);
        viewSettings.projectionMatrix = Perspective(1.0472f, 0.1f, 50.0f);
        viewSettings.viewport = FluxionViewport{ 0.0f, 0.0f, (f32)kWidth, (f32)kHeight, 0.0f, 1.0f };
        viewSettings.scissor = FluxionScissorRect{ 0, 0, kWidth, kHeight };
        viewSettings.renderTarget = target;
        viewSettings.layerMask = 0xFFFFFFFFu;
        viewSettings.ambientColor = FluxionVec3{ kAmbient, kAmbient, kAmbient };
        viewSettings.exposure = 1.0f;
        viewSettings.tonemapWhitePoint = 0.0f;
        viewSettings.encodeOutputToSRGB = false;

        FluxionRenderViewHandle view = Fluxion_RenderView_Create(device, &viewSettings);
        if (!FLUXION_HANDLE_IS_VALID(view)) return false;

        Fluxion_RenderView_UpdateFrameConstants(view);
        Fluxion_Renderer_BeginFrame(renderer, view);

        FluxionRenderWorld world{};
        if (!Fluxion_RenderWorld_Init(&world))
        {
            Fluxion_RenderView_Destroy(view);
            return false;
        }

        auto addObject = [&](FluxionMeshBufferHandle mesh) {
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
        addObject(floorMesh);
        addObject(wallMesh);

        Fluxion_RHI_CommandList_Begin(cmd);
        Fluxion_Renderer_UpdateEnvironment(renderer, view, cmd);
        Fluxion_Renderer_SubmitRenderWorld(renderer, &world);
        Fluxion_Renderer_UploadScene(renderer, cmd);

        const FluxionRHIBufferHandle noBuffer = Fluxion::Foundation::NoHandle<FluxionRHIBufferHandle>();
        const FluxionRHIResourceState wasColor = first ? FLUXION_RHI_RESOURCE_STATE_UNDEFINED : FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE;
        const FluxionRHIResourceState wasDepth = first ? FLUXION_RHI_RESOURCE_STATE_UNDEFINED : FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE;

        FluxionRHIBarrier atlasToRead = { Fluxion_RenderView_GetShadowAtlasTexture(view), noBuffer,
                                          FLUXION_RHI_RESOURCE_STATE_UNDEFINED, FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
        Fluxion_RHI_CommandList_Barrier(cmd, &atlasToRead, 1);

        FluxionRHIBarrier depthToWrite = { depth, noBuffer, wasDepth, FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE, 0, 0 };
        Fluxion_RHI_CommandList_Barrier(cmd, &depthToWrite, 1);

        FluxionRHIBarrier colorToTarget = { color, noBuffer, wasColor, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, 0, 0 };
        Fluxion_RHI_CommandList_Barrier(cmd, &colorToTarget, 1);

        FluxionForwardOpaquePass_Execute(cmd, Fluxion_Renderer_GetForwardOpaquePassUserData(renderer));

        FluxionRHIBarrier colorToSource = { color, noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET,
                                            FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE, 0, 0 };
        Fluxion_RHI_CommandList_Barrier(cmd, &colorToSource, 1);
        Fluxion_RHI_CommandList_CopyTextureToBuffer(cmd, color, 0, 0, colorReadback, 0);

        const FluxionRHITextureHandle occlusionTexture = Fluxion_Renderer_GetAmbientOcclusionTexture(renderer);
        const bool wantOcclusion = outOcclusion != nullptr && occlude && FLUXION_HANDLE_IS_VALID(occlusionTexture);
        if (wantOcclusion)
        {
            FluxionRHIBarrier toSource = { occlusionTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_SHADER_READ,
                                           FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE, 0, 0 };
            Fluxion_RHI_CommandList_Barrier(cmd, &toSource, 1);
            Fluxion_RHI_CommandList_CopyTextureToBuffer(cmd, occlusionTexture, 0, 0, occlusionReadback, 0);

            FluxionRHIBarrier back = { occlusionTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE,
                                       FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
            Fluxion_RHI_CommandList_Barrier(cmd, &back, 1);
        }

        Fluxion_Renderer_EndFrame(renderer, cmd);
        Fluxion_RHI_CommandList_End(cmd);

        FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(device, false);
        Fluxion_RHI_Queue_Submit(queue, &cmd, 1, fence);
        const bool finished = Fluxion_RHI_WaitForFence(fence);
        Fluxion_RHI_DestroyFence(fence);

        bool read = false;
        if (finished)
        {
            if (const u8* mapped = (const u8*)Fluxion_RHI_MapBuffer(colorReadback))
            {
                outColor.assign((usize)kWidth * kHeight * 4u, 0.0f);
                for (u32 y = 0; y < kHeight; ++y)
                {
                    const u8* row = mapped + (usize)y * colorAligned;
                    for (u32 x = 0; x < kWidth * 4u; ++x)
                    {
                        u16 half = 0;
                        std::memcpy(&half, row + (usize)x * sizeof(u16), sizeof(half));
                        outColor[(usize)y * kWidth * 4u + x] = HalfBitsToFloat(half);
                    }
                }
                Fluxion_RHI_UnmapBuffer(colorReadback);
                read = true;
            }

            if (wantOcclusion)
            {
                if (const u8* mapped = (const u8*)Fluxion_RHI_MapBuffer(occlusionReadback))
                {
                    outOcclusion->assign((usize)kWidth * kHeight * 4u, 0);
                    for (u32 y = 0; y < kHeight; ++y)
                    {
                        std::memcpy(&(*outOcclusion)[(usize)y * kWidth * 4u], mapped + (usize)y * occlusionAligned, occlusionRowBytes);
                    }
                    Fluxion_RHI_UnmapBuffer(occlusionReadback);
                }
            }
        }

        Fluxion_RHI_Device_CollectGarbage(device);
        Fluxion_RenderWorld_Shutdown(&world);
        Fluxion_RenderView_Destroy(view);
        return read;
    };

    std::vector<f32> withoutPrepass;

    // THE FLOOR IS A METRE BELOW THE EYE for the frames the corner is
    // measured in, and a HAND'S WIDTH below it for the last one -- which
    // is the view a person actually looks at a floor from in a game, and
    // the one where every neighbouring pixel is metres further away than
    // the last.
    std::vector<f32> grazingColor;
    std::vector<u8> grazingOcclusion;

    const bool ok = renderOnce(0.0f, true, false, true, withoutOcclusion, nullptr) &&
                    renderOnce(0.0f, true, true, false, withOcclusion, &occlusionMap) &&
                    renderOnce(0.0f, false, false, false, withoutPrepass, nullptr) &&
                    renderOnce(-0.9f, true, true, false, grazingColor, &grazingOcclusion);

    TEST_CHECK(ctx, ok);

    if (ok)
    {
        // --- THE SAME PICTURE WITH AND WITHOUT THE PREPASS ---------------
        //
        // The pass that records surfaces draws this geometry into the same
        // depth buffer the lighting then tests against, and the lighting
        // stops clearing it -- which is where the early rejection comes
        // from. It works only if the two passes agree about depth TO THE
        // BIT, and they are two separately compiled programs. Where they
        // disagree by the smallest amount the wrong way, the lighting
        // rejects a pixel that is really there, and a solid surface comes
        // out with holes in it that the sky shows through.
        //
        // Counted rather than compared pixel for pixel: what matters is
        // whether the surface is THERE, not whether its colour is
        // identical to the last bit.
        u32 drawnWith = 0;
        u32 drawnWithout = 0;
        for (u32 y = 0; y < kHeight; ++y)
        {
            for (u32 x = 0; x < kWidth; ++x)
            {
                if (Brightness(withoutOcclusion, x, y) >= kEmissive * 0.5f) ++drawnWith;
                if (Brightness(withoutPrepass, x, y) >= kEmissive * 0.5f) ++drawnWithout;
            }
        }

        FLUXION_LOG_INFO("RenderCoreTests",
                         "%s: %u pixels are covered with the surfaces recorded first and %u without.",
                         backendName, drawnWith, drawnWithout);

        TEST_CHECK(ctx, drawnWithout > 0);
        TEST_CHECK(ctx, drawnWith == drawnWithout);
    }

    if (ok && !occlusionMap.empty())
    {
        // THE DARKEST PLACE, FOUND RATHER THAN CALCULATED. Where the
        // crease lands on the screen is a question about a projection;
        // what this test is about is whether anywhere at all came out
        // occluded, and whether the open floor did not.
        u32 darkestX = 0;
        u32 darkestY = 0;
        f32 darkest = 2.0f;
        f32 mostOpen = 0.0f;
        u32 openCount = 0;
        u32 drawnCount = 0;

        for (u32 y = 0; y < kHeight; ++y)
        {
            for (u32 x = 0; x < kWidth; ++x)
            {
                // Only where something was actually drawn: the background
                // has no surface recorded and reads as fully open, which
                // would swamp the count below.
                if (Brightness(withoutOcclusion, x, y) < kEmissive * 0.5f) continue;
                ++drawnCount;

                const f32 value = Occlusion(occlusionMap, x, y);
                if (value < darkest)
                {
                    darkest = value;
                    darkestX = x;
                    darkestY = y;
                }
                if (value > mostOpen) mostOpen = value;
                if (value > 0.95f) ++openCount;
            }
        }

        // HOW MUCH IT JUMPS FROM ONE PIXEL TO THE NEXT.
        //
        // This is the one a user found and a number did not. Too few
        // samples come out as noise, the blur after the search is
        // supposed to take it out, and on a surface seen at a grazing
        // angle -- a floor, which is most of what an occlusion pass is
        // looked at on -- the blur's own edge test collapses, because
        // neighbouring pixels there really are far apart in depth. What
        // survives is patches, and a picture with patches on the floor is
        // worse than no occlusion at all.
        //
        // Measured across neighbours rather than as a variance over the
        // frame: the frame SHOULD vary, from the open floor to the
        // crease. What it should not do is vary between one pixel and the
        // one beside it.
        f32 worstJump = 0.0f;
        f64 totalJump = 0.0;
        u32 jumpCount = 0;
        for (u32 y = 0; y < kHeight; ++y)
        {
            for (u32 x = 0; x + 1 < kWidth; ++x)
            {
                if (Brightness(withoutOcclusion, x, y) < kEmissive * 0.5f) continue;
                if (Brightness(withoutOcclusion, x + 1, y) < kEmissive * 0.5f) continue;

                const f32 jump = std::fabs(Occlusion(occlusionMap, x, y) - Occlusion(occlusionMap, x + 1, y));
                if (jump > worstJump) worstJump = jump;
                totalJump += (f64)jump;
                ++jumpCount;
            }
        }
        const f32 averageJump = jumpCount > 0 ? (f32)(totalJump / (f64)jumpCount) : 0.0f;

        const f32 litWithout = Brightness(withoutOcclusion, darkestX, darkestY);
        const f32 litWith = Brightness(withOcclusion, darkestX, darkestY);

        FLUXION_LOG_INFO("RenderCoreTests",
                         "%s: of %u drawn pixels %u are open, the most open reading %.3f; the most occluded reads %.3f, and there the picture goes "
                         "from %.4f to %.4f with the emission at %.4f. Neighbour to neighbour it moves %.4f on average "
                         "and %.4f at worst.",
                         backendName, drawnCount, openCount, (f64)mostOpen, (f64)darkest, (f64)litWithout, (f64)litWith, (f64)kEmissive,
                         (f64)averageJump, (f64)worstJump);

        TEST_CHECK(ctx, drawnCount > (kWidth * kHeight) / 8u);

        // SOMEWHERE IS OCCLUDED. A corner exists in this scene, and a
        // pass that measured nothing would leave every pixel at one.
        TEST_CHECK(ctx, darkest < 0.9f);

        // AND SOMEWHERE IS NOT OCCLUDED AT ALL. This is the other half of
        // the claim and the one with a real number behind it: a flat
        // surface with nothing near it sees the whole sky, so the most
        // open pixel in the frame has to be all but exactly one. A pass
        // that darkened indiscriminately -- or whose distances came out
        // inverted -- fails here while passing the line above.
        TEST_CHECK(ctx, mostOpen > 0.98f);

        // AND A LARGE PART OF THE FRAME IS LIKE THAT. A share rather than
        // a physical constant: how much of a frame this small a corner
        // takes up is a question about where the camera was put, so what
        // is checked is that the effect is a feature of the picture and
        // not the whole of it.
        TEST_CHECK(ctx, openCount > drawnCount / 3u);

        // THE PICTURE FOLLOWS. The occlusion is worth nothing if the
        // lighting does not read it.
        TEST_CHECK(ctx, litWith < litWithout * 0.95f);

        // AND IT TOOK ONLY THE SKY. However much of the hemisphere is
        // gone, what the surface EMITS is still there -- emission is not
        // indirect light, and a pass that multiplied the finished picture
        // would have eaten into it.
        TEST_CHECK(ctx, litWith > kEmissive * 0.95f);

        // AND IT IS SMOOTH. A pass whose samples are too few, or whose
        // radius came out a fraction of what was asked for, leaves the
        // noise it was supposed to average away -- and the blur cannot
        // help on the surface it matters most on. An average step of a
        // fiftieth between neighbours is below what an eye picks out of a
        // flat surface; the crease itself is allowed to move faster,
        // which is what the worst-case allowance is for.
        TEST_CHECK(ctx, averageJump < 0.02f);
        TEST_CHECK(ctx, worstJump < 0.35f);
    }

    if (ok && !grazingOcclusion.empty())
    {
        // THE SAME SMOOTHNESS, ON THE SURFACE IT IS HARDEST ON.
        //
        // Seen almost edge-on, two neighbouring pixels of a flat floor are
        // genuinely metres apart in distance -- so a denoise that decides
        // "same surface or not" by an absolute distance rejects every
        // neighbour it has and does nothing at all. What is left is the
        // raw noise of a handful of samples, in patches, on the largest
        // flat thing in the picture.
        f32 worstJump = 0.0f;
        f64 totalJump = 0.0;
        u32 jumpCount = 0;
        for (u32 y = 0; y < kHeight; ++y)
        {
            for (u32 x = 0; x + 1 < kWidth; ++x)
            {
                if (Brightness(grazingColor, x, y) < kEmissive * 0.5f) continue;
                if (Brightness(grazingColor, x + 1, y) < kEmissive * 0.5f) continue;

                const f32 jump = std::fabs(Occlusion(grazingOcclusion, x, y) - Occlusion(grazingOcclusion, x + 1, y));
                if (jump > worstJump) worstJump = jump;
                totalJump += (f64)jump;
                ++jumpCount;
            }
        }
        const f32 averageJump = jumpCount > 0 ? (f32)(totalJump / (f64)jumpCount) : 0.0f;

        FLUXION_LOG_INFO("RenderCoreTests",
                         "%s: seen almost edge-on, the occlusion moves %.4f between neighbours on average and %.4f at "
                         "worst, over %u pairs.",
                         backendName, (f64)averageJump, (f64)worstJump, jumpCount);

        TEST_CHECK(ctx, jumpCount > 1000u);
        TEST_CHECK(ctx, averageJump < 0.02f);
        TEST_CHECK(ctx, worstJump < 0.35f);
    }

    Fluxion_Renderer_Destroy(renderer);
    Fluxion_Material_Destroy(material);
    Fluxion_RHI_DestroySampler(sampler);
    Fluxion_TextureDefaults_Shutdown();
    Fluxion_RenderPipeline_Destroy(pipeline);
    if (FLUXION_HANDLE_IS_VALID(prepassProgram)) Fluxion_ShaderProgram_Destroy(prepassProgram);
    Fluxion_ShaderProgram_Destroy(program);
    Fluxion_MeshBuffer_Destroy(floorMesh);
    Fluxion_MeshBuffer_Destroy(wallMesh);
    Fluxion_RenderTarget_Destroy(target);
    Fluxion_RHI_DestroyBuffer(colorReadback);
    Fluxion_RHI_DestroyBuffer(occlusionReadback);
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

extern "C" void Test_AmbientOcclusionGPU_Run(TestContext* ctx)
{
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No shader compiler on this machine -- the occlusion was NOT measured here.");
        return;
    }

    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_OPENGL, "OpenGL");
}
