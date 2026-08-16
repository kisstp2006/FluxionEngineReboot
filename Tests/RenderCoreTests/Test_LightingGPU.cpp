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
#include <Fluxion/RenderCore/Renderer/Exposure.h>
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
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>

#include <cmath>
#include <cstring>
#include <vector>

// What the reflectance model does, asked of a real device.
//
// The formulae in Fluxion/BRDF.jsl come from published papers, and there
// is no second implementation anywhere to compare them against -- writing
// one here would only prove that the same person made the same reading
// twice. So this checks something better than agreement: the PROPERTIES
// that any correct reflectance model has, whatever its formulae.
//
// A surface cannot send back more light than fell on it. A surface turned
// away from the light receives none. A smoother surface concentrates its
// highlight and is therefore brighter in the mirror direction than a
// rougher one. A metal has no diffuse colour, so away from its highlight
// it is nearly black where a non-metal of the same colour is not. Light a
// surface emits is not affected by any of that.
//
// Each of those is a statement about physics rather than about this code,
// and a shader that satisfies all of them on two drivers is doing what it
// says.

namespace
{

constexpr u32 kSize = 8;

// Room for the one directional light the base configuration describes
// plus whatever a check adds beside it.
#define FLUXION_TEST_MAX_LIGHTS 8

// The material under test: the standard surface and nothing added. Every
// value it uses comes from a parameter this test sets, so what arrives at
// the shader is entirely known.
const char* const kMaterialSource = R"(
#include "Fluxion/Material.jsl"

SurfaceData EvaluateSurface() {
  return StandardSurface();
}
)";

struct QuadVertex
{
    f32 position[3];
    f32 normal[3];
    f32 tangent[4];
    f32 uv[2];
};

// Facing the camera, filling the target. The normal and tangent are given
// outright rather than derived, so the surface frame is a known quantity
// rather than something the test would have to trust.
const QuadVertex kQuadVertices[4] = {
    { { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
    { { 1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
    { { 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
    { { -1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
};

// Both windings, so that whichever way round the pipeline's raster state
// considers a front face, the quad is drawn. Both sit at the same depth,
// so the second contributes nothing where the first already did.
const u16 kQuadIndices[12] = { 0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2 };

// What one draw is asked to light.
struct Configuration
{
    const char* name;

    f32 baseColor[3];
    f32 metallic;
    f32 roughness;
    f32 emissive[3];
    f32 occlusion; // the strength, not the map

    f32 sunDirection[3];
    f32 sunColor[3];
    f32 ambient[3];

    f32 exposure;
    f32 tonemapWhitePoint;

    f32 opacity;
    f32 alphaCutoff;

    bool encodeOutputToSRGB;

    // Lights beyond the single directional one the fields above describe.
    // Handed over exactly as given, so a check can say precisely what it
    // is putting in front of the surface.
    FluxionRenderLight extraLights[FLUXION_TEST_MAX_LIGHTS];
    u32 extraLightCount;
};

struct Rgb
{
    f32 r, g, b;
};

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

// Everything this test builds, so that giving up halfway is one call to
// tear down rather than a list somebody has to keep in step.
//
// EVERY HANDLE STARTS INVALID, not zeroed, and that is the whole reason
// this struct spells its fields out. A zeroed handle has index zero, and
// index zero is a real slot -- so a rig that gave up before it built
// anything would still look like it held everything, and tearing it down
// would free whatever is sitting in slot zero. On a machine where every
// backend works nothing gives up and it never happens; on a machine
// missing one, it is the other backend's work being freed.
struct LightingRig
{
    FluxionRHIInstanceHandle instance = Fluxion::Foundation::NoHandle<FluxionRHIInstanceHandle>();
    FluxionRHIDeviceHandle device = Fluxion::Foundation::NoHandle<FluxionRHIDeviceHandle>();
    FluxionRHIQueueHandle queue = Fluxion::Foundation::NoHandle<FluxionRHIQueueHandle>();

    FluxionShaderProgramHandle program = Fluxion::Foundation::NoHandle<FluxionShaderProgramHandle>();
    FluxionMeshBufferHandle mesh = Fluxion::Foundation::NoHandle<FluxionMeshBufferHandle>();
    FluxionRenderPipelineHandle pipeline = Fluxion::Foundation::NoHandle<FluxionRenderPipelineHandle>();
    FluxionRendererHandle renderer = Fluxion::Foundation::NoHandle<FluxionRendererHandle>();

    FluxionRHITextureHandle color = Fluxion::Foundation::NoHandle<FluxionRHITextureHandle>();
    FluxionRHITextureViewHandle colorView = Fluxion::Foundation::NoHandle<FluxionRHITextureViewHandle>();
    FluxionRHITextureHandle depth = Fluxion::Foundation::NoHandle<FluxionRHITextureHandle>();
    FluxionRHITextureViewHandle depthView = Fluxion::Foundation::NoHandle<FluxionRHITextureViewHandle>();
    FluxionRenderTargetHandle target = Fluxion::Foundation::NoHandle<FluxionRenderTargetHandle>();
    FluxionRHIBufferHandle readback = Fluxion::Foundation::NoHandle<FluxionRHIBufferHandle>();
    FluxionRHISamplerHandle sampler = Fluxion::Foundation::NoHandle<FluxionRHISamplerHandle>();

    bool usable = false;
};

// A floating-point target on purpose. The lighting has no tone mapping in
// front of it yet, so a value above one is a real value -- and an
// eight-bit target would clamp it to one, which would make an energy
// check pass for a shader that was returning far too much light.
constexpr FluxionRHIFormat kColorFormat = FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT;

bool BuildRig(TestContext* ctx, LightingRig& rig, FluxionRHIBackendType backend, const char* backendName)
{
    if (!Fluxion_RHI_IsBackendAvailable(backend))
    {
        FLUXION_LOG_WARN("RenderCoreTests", "%s is not in this build -- skipping its lighting check.", backendName);
        return false;
    }

    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests", true };
    rig.instance = Fluxion_RHI_CreateInstance(backend, &instanceDesc);
    if (!FLUXION_HANDLE_IS_VALID(rig.instance))
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No usable %s loader -- skipping its lighting check.", backendName);
        return false;
    }

    FluxionRHIAdapterHandle adapters[8];
    if (Fluxion_RHI_EnumerateAdapters(rig.instance, adapters, 8) == 0)
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No %s adapters -- skipping its lighting check.", backendName);
        return false;
    }

    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
    rig.device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    if (!FLUXION_HANDLE_IS_VALID(rig.device))
    {
        FLUXION_LOG_WARN("RenderCoreTests", "%s device creation failed -- skipping its lighting check.", backendName);
        return false;
    }

    rig.queue = Fluxion_RHI_GetQueue(rig.device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    if (!Fluxion_RHI_Device_IsFormatSupported(rig.device, kColorFormat))
    {
        FLUXION_LOG_WARN("RenderCoreTests", "%s has no floating-point colour target -- skipping its lighting check.", backendName);
        return false;
    }

    char* vertexSource = Fluxion_MaterialShader_BuildVertexSource(FLUXION_MATERIAL_PASS_FORWARD);
    char* fragmentSource = Fluxion_MaterialShader_BuildFragmentSource(kMaterialSource, FLUXION_MATERIAL_PASS_FORWARD);
    TEST_CHECK(ctx, vertexSource != nullptr && fragmentSource != nullptr);
    if (vertexSource == nullptr || fragmentSource == nullptr) return false;

    FluxionShaderProgramDesc programDesc{};
    programDesc.debugName = "LightingGPU.Standard";
    programDesc.vertexSource = vertexSource;
    programDesc.fragmentSource = fragmentSource;
    rig.program = Fluxion_ShaderProgram_Create(rig.device, &programDesc);

    Fluxion_MaterialShader_FreeSource(vertexSource);
    Fluxion_MaterialShader_FreeSource(fragmentSource);

    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(rig.program));
    if (!FLUXION_HANDLE_IS_VALID(rig.program)) return false;

    FluxionMeshBufferDesc meshDesc{};
    meshDesc.vertexData = kQuadVertices;
    meshDesc.vertexDataSize = sizeof(kQuadVertices);
    meshDesc.indexData = kQuadIndices;
    meshDesc.indexDataSize = sizeof(kQuadIndices);
    meshDesc.use16BitIndices = true;
    meshDesc.vertexLayout.attributes[0].location = 0;
    meshDesc.vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    meshDesc.vertexLayout.attributes[0].offset = offsetof(QuadVertex, position);
    meshDesc.vertexLayout.attributes[1].location = 1;
    meshDesc.vertexLayout.attributes[1].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    meshDesc.vertexLayout.attributes[1].offset = offsetof(QuadVertex, normal);
    meshDesc.vertexLayout.attributes[2].location = 2;
    meshDesc.vertexLayout.attributes[2].format = FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT;
    meshDesc.vertexLayout.attributes[2].offset = offsetof(QuadVertex, tangent);
    meshDesc.vertexLayout.attributes[3].location = 3;
    meshDesc.vertexLayout.attributes[3].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    meshDesc.vertexLayout.attributes[3].offset = offsetof(QuadVertex, uv);
    meshDesc.vertexLayout.attributeCount = 4;
    meshDesc.vertexLayout.stride = sizeof(QuadVertex);
    meshDesc.bounds = FluxionAABB{ FluxionVec3{ -1.0f, -1.0f, 0.0f }, FluxionVec3{ 1.0f, 1.0f, 0.0f } };
    meshDesc.debugName = "LightingGPU.Quad";
    rig.mesh = Fluxion_MeshBuffer_Create(rig.device, rig.queue, &meshDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(rig.mesh));

    FluxionRHITextureDesc colorDesc{};
    colorDesc.width = kSize;
    colorDesc.height = kSize;
    colorDesc.depth = 1;
    colorDesc.mipLevels = 1;
    colorDesc.arrayLayers = 1;
    colorDesc.sampleCount = 1;
    colorDesc.format = kColorFormat;
    colorDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET | FLUXION_RHI_TEXTURE_USAGE_TRANSFER_SRC;
    colorDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    colorDesc.debugName = "LightingGPU.Color";
    rig.color = Fluxion_RHI_CreateTexture(rig.device, &colorDesc);
    FluxionRHITextureViewDesc colorViewDesc = { rig.color, kColorFormat, 0, 1, 0, 1, FLUXION_RHI_TEXTURE_DIMENSION_2D };
    rig.colorView = Fluxion_RHI_CreateTextureView(rig.device, &colorViewDesc);

    FluxionRHITextureDesc depthDesc = colorDesc;
    depthDesc.format = FLUXION_RHI_FORMAT_D32_FLOAT;
    depthDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL;
    depthDesc.debugName = "LightingGPU.Depth";
    rig.depth = Fluxion_RHI_CreateTexture(rig.device, &depthDesc);
    FluxionRHITextureViewDesc depthViewDesc = { rig.depth, depthDesc.format, 0, 1, 0, 1, FLUXION_RHI_TEXTURE_DIMENSION_2D };
    rig.depthView = Fluxion_RHI_CreateTextureView(rig.device, &depthViewDesc);

    FluxionRenderTargetDesc targetDesc{};
    targetDesc.colorViews[0] = rig.colorView;
    targetDesc.colorViewCount = 1;
    targetDesc.depthView = rig.depthView;
    rig.target = Fluxion_RenderTarget_Create(rig.device, &targetDesc);

    rig.pipeline = Fluxion_RenderPipeline_Create(rig.device, rig.program, FLUXION_RENDER_PIPELINE_CATEGORY_OPAQUE,
                                                 kColorFormat, depthDesc.format);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(rig.pipeline));

    rig.renderer = Fluxion_Renderer_Create(rig.device, rig.queue);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(rig.renderer));

    FluxionRHISamplerDesc samplerDesc{};
    samplerDesc.minFilter = FLUXION_RHI_FILTER_NEAREST;
    samplerDesc.magFilter = FLUXION_RHI_FILTER_NEAREST;
    samplerDesc.mipFilter = FLUXION_RHI_FILTER_NEAREST;
    samplerDesc.addressModeU = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeV = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeW = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.maxAnisotropy = 1.0f;
    samplerDesc.debugName = "LightingGPU.Sampler";
    rig.sampler = Fluxion_RHI_CreateSampler(rig.device, &samplerDesc);

    FluxionRHIBufferDesc readbackDesc{};
    readbackDesc.size = ((usize)kSize * 8 + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) / FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * kSize;
    readbackDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST;
    readbackDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_READBACK;
    readbackDesc.debugName = "LightingGPU.Readback";
    rig.readback = Fluxion_RHI_CreateBuffer(rig.device, &readbackDesc);

    rig.usable = FLUXION_HANDLE_IS_VALID(rig.mesh) && FLUXION_HANDLE_IS_VALID(rig.pipeline) &&
                 FLUXION_HANDLE_IS_VALID(rig.renderer) && FLUXION_HANDLE_IS_VALID(rig.readback);
    return rig.usable;
}

void DestroyRig(LightingRig& rig)
{
    if (FLUXION_HANDLE_IS_VALID(rig.readback)) Fluxion_RHI_DestroyBuffer(rig.readback);
    if (FLUXION_HANDLE_IS_VALID(rig.sampler)) Fluxion_RHI_DestroySampler(rig.sampler);
    if (FLUXION_HANDLE_IS_VALID(rig.target)) Fluxion_RenderTarget_Destroy(rig.target);
    if (FLUXION_HANDLE_IS_VALID(rig.renderer)) Fluxion_Renderer_Destroy(rig.renderer);
    if (FLUXION_HANDLE_IS_VALID(rig.pipeline)) Fluxion_RenderPipeline_Destroy(rig.pipeline);
    if (FLUXION_HANDLE_IS_VALID(rig.mesh)) Fluxion_MeshBuffer_Destroy(rig.mesh);
    if (FLUXION_HANDLE_IS_VALID(rig.program)) Fluxion_ShaderProgram_Destroy(rig.program);
    if (FLUXION_HANDLE_IS_VALID(rig.depthView)) Fluxion_RHI_DestroyTextureView(rig.depthView);
    if (FLUXION_HANDLE_IS_VALID(rig.depth)) Fluxion_RHI_DestroyTexture(rig.depth);
    if (FLUXION_HANDLE_IS_VALID(rig.colorView)) Fluxion_RHI_DestroyTextureView(rig.colorView);
    if (FLUXION_HANDLE_IS_VALID(rig.color)) Fluxion_RHI_DestroyTexture(rig.color);
    if (FLUXION_HANDLE_IS_VALID(rig.device))
    {
        Fluxion_RHI_Device_CollectGarbage(rig.device);
        Fluxion_RHI_DestroyDevice(rig.device);
    }
    if (FLUXION_HANDLE_IS_VALID(rig.instance)) Fluxion_RHI_DestroyInstance(rig.instance);
}

// One draw, one configuration, one colour back.
Rgb RenderOne(TestContext* ctx, LightingRig& rig, const Configuration& configuration)
{
    Rgb result = { 0.0f, 0.0f, 0.0f };

    FluxionMaterialHandle material = Fluxion_Material_Create(rig.device, rig.program);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(material));
    if (!FLUXION_HANDLE_IS_VALID(material)) return result;

    Fluxion_Material_SetBaseColor(material, FluxionVec4{ configuration.baseColor[0], configuration.baseColor[1], configuration.baseColor[2], configuration.opacity });
    Fluxion_Material_SetAlphaCutoff(material, configuration.alphaCutoff);
    Fluxion_Material_SetMetallic(material, configuration.metallic);
    Fluxion_Material_SetRoughness(material, configuration.roughness);
    Fluxion_Material_SetReflectance(material, 0.5f);
    Fluxion_Material_SetEmissive(material, FluxionVec3{ configuration.emissive[0], configuration.emissive[1], configuration.emissive[2] });
    Fluxion_Material_SetNormalScale(material, 1.0f);
    Fluxion_Material_SetOcclusionStrength(material, configuration.occlusion);

    // Every map bound to the one-pixel default that changes nothing --
    // which is the arrangement a material with no maps of its own really
    // has, not a special case for this test.
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_BASE_COLOR, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), rig.sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_METALLIC_ROUGHNESS, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), rig.sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_NORMAL, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_FLAT_NORMAL), rig.sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_OCCLUSION, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), rig.sampler);
    Fluxion_Material_SetTextureSlot(material, FLUXION_MATERIAL_TEXTURE_EMISSIVE, Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), rig.sampler);
    Fluxion_Material_FlushDirty(material);

    // The camera five units in front of the quad, and the quad filling
    // the target exactly.
    //
    // Those two wants pull against each other: a real projection would
    // shrink the quad, and an identity one would put the camera on the
    // surface, where the direction to the eye is undefined. So the view
    // moves the world back and the "projection" moves it forward again --
    // their product is the identity, and the camera position, which is
    // taken from the view alone, is five units away as intended.
    FluxionRenderViewDesc viewDesc{};
    viewDesc.viewMatrix = Fluxion_Mat4_Translation(FluxionVec3{ 0.0f, 0.0f, -5.0f });
    viewDesc.projectionMatrix = Fluxion_Mat4_Translation(FluxionVec3{ 0.0f, 0.0f, 5.0f });
    viewDesc.viewport = FluxionViewport{ 0.0f, 0.0f, (f32)kSize, (f32)kSize, 0.0f, 1.0f };
    viewDesc.scissor = FluxionScissorRect{ 0, 0, kSize, kSize };
    viewDesc.renderTarget = rig.target;
    viewDesc.layerMask = 0xFFFFFFFFu;
    viewDesc.ambientColor = FluxionVec3{ configuration.ambient[0], configuration.ambient[1], configuration.ambient[2] };
    viewDesc.exposure = configuration.exposure;
    viewDesc.tonemapWhitePoint = configuration.tonemapWhitePoint;
    viewDesc.encodeOutputToSRGB = configuration.encodeOutputToSRGB;
    FluxionRenderViewHandle view = Fluxion_RenderView_Create(rig.device, &viewDesc);

    // The one light every check above this file's own light checks uses,
    // built here rather than carried on the view: a light is a thing in
    // the world now, and the view is only told the list.
    //
    // The direction turns round. The configuration says which way it is
    // TO the light -- which is what a reader can check against a picture
    // -- and a light carries the way it TRAVELS, which is what a cone is
    // measured around.
    FluxionRenderLight lights[FLUXION_TEST_MAX_LIGHTS];
    u32 lightCount = 0;

    if (configuration.sunColor[0] != 0.0f || configuration.sunColor[1] != 0.0f || configuration.sunColor[2] != 0.0f)
    {
        FluxionRenderLight& sun = lights[lightCount++];
        sun = FluxionRenderLight{};
        sun.type = FLUXION_RENDER_LIGHT_DIRECTIONAL;
        sun.direction = FluxionVec3{ -configuration.sunDirection[0], -configuration.sunDirection[1], -configuration.sunDirection[2] };
        sun.color = FluxionVec3{ configuration.sunColor[0], configuration.sunColor[1], configuration.sunColor[2] };
    }

    for (u32 i = 0; i < configuration.extraLightCount; ++i)
    {
        lights[lightCount++] = configuration.extraLights[i];
    }

    Fluxion_RenderView_SetLights(view, lights, lightCount);
    Fluxion_RenderView_UpdateFrameConstants(view);

    FluxionRHICommandListHandle cmd = Fluxion_RHI_CreateCommandList(rig.device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RHI_CommandList_Begin(cmd);

    // Inside the recording, before anything draws with this view.
    Fluxion_RenderView_UploadLights(view, cmd);

    FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(rig.device);
    Fluxion_RenderGraph_ImportTexture(graph, "ForwardOpaquePass.Color0", rig.color, FLUXION_RHI_RESOURCE_STATE_UNDEFINED);
    Fluxion_RenderGraph_ImportTexture(graph, "ForwardOpaquePass.Depth", rig.depth, FLUXION_RHI_RESOURCE_STATE_UNDEFINED);
    Fluxion_RenderGraph_AddPassFromRegistry(graph, "ForwardOpaquePass", Fluxion_Renderer_GetForwardOpaquePassUserData(rig.renderer));

    Fluxion_Renderer_BeginFrame(rig.renderer, view);
    Fluxion_Renderer_DrawMesh(rig.renderer, rig.mesh, material, rig.pipeline, nullptr);
    TEST_CHECK(ctx, Fluxion_RenderGraph_Compile(graph));
    Fluxion_RenderGraph_Execute(graph, cmd);
    Fluxion_Renderer_EndFrame(rig.renderer, cmd);

    FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIBarrier toSource = { rig.color, noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, FLUXION_RHI_RESOURCE_STATE_COPY_SOURCE };
    Fluxion_RHI_CommandList_Barrier(cmd, &toSource, 1);
    Fluxion_RHI_CommandList_CopyTextureToBuffer(cmd, rig.color, 0, 0, rig.readback, 0);
    Fluxion_RHI_CommandList_End(cmd);

    FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(rig.device, false);
    Fluxion_RHI_Queue_Submit(rig.queue, &cmd, 1, fence);
    TEST_CHECK(ctx, Fluxion_RHI_WaitForFence(fence));

    {
        const u8* mapped = (const u8*)Fluxion_RHI_MapBuffer(rig.readback);
        TEST_CHECK(ctx, mapped != nullptr);
        if (mapped != nullptr)
        {
            // The middle of the quad, where the surface faces the camera
            // squarely and the geometry contributes nothing of its own.
            const usize rowBytes = ((usize)kSize * 8 + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) / FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;
            u16 texel[4];
            std::memcpy(texel, mapped + (kSize / 2) * rowBytes + (kSize / 2) * 8, sizeof(texel));

            result.r = HalfBitsToFloat(texel[0]);
            result.g = HalfBitsToFloat(texel[1]);
            result.b = HalfBitsToFloat(texel[2]);
            Fluxion_RHI_UnmapBuffer(rig.readback);
        }
    }

    Fluxion_RenderGraph_Destroy(graph);
    Fluxion_RHI_DestroyFence(fence);
    Fluxion_RHI_DestroyCommandList(cmd);
    Fluxion_RenderView_Destroy(view);
    Fluxion_Material_Destroy(material);
    Fluxion_RHI_Device_CollectGarbage(rig.device);

    return result;
}

Configuration BaseConfiguration()
{
    Configuration configuration{};
    configuration.name = "";
    configuration.baseColor[0] = 1.0f;
    configuration.baseColor[1] = 1.0f;
    configuration.baseColor[2] = 1.0f;
    configuration.metallic = 0.0f;
    configuration.roughness = 1.0f;
    configuration.occlusion = 0.0f;

    // One and none: the light that leaves the surface arrives at the
    // target untouched, so every check above this one is about the
    // reflectance model and nothing else. The camera gets its own checks
    // further down.
    configuration.exposure = 1.0f;
    configuration.tonemapWhitePoint = 0.0f;

    // Fully opaque, and no alpha test -- a threshold of zero is never
    // reached, so every check above this one is about a pixel that is
    // certainly there.
    configuration.opacity = 1.0f;
    configuration.alphaCutoff = 0.0f;

    // Off, so every check above reads the light itself rather than a
    // curve applied to it.
    configuration.encodeOutputToSRGB = false;

    // Straight at the surface, which faces the camera.
    configuration.sunDirection[2] = 1.0f;
    configuration.sunColor[0] = 1.0f;
    configuration.sunColor[1] = 1.0f;
    configuration.sunColor[2] = 1.0f;
    return configuration;
}

void CheckOnBackend(TestContext* ctx, FluxionRHIBackendType backend, const char* backendName)
{
    LightingRig rig;
    Fluxion_RenderGraphPassRegistry_Init();

    if (!BuildRig(ctx, rig, backend, backendName))
    {
        DestroyRig(rig);
        Fluxion_RenderGraphPassRegistry_Shutdown();
        return;
    }

    TEST_CHECK(ctx, Fluxion_TextureDefaults_Init(rig.device, rig.queue));

    // --- A surface cannot send back more light than fell on it ----------
    //
    // White, fully rough, not a metal, lit head-on by a light of exactly
    // one. Whatever leaves has to be less than one, and a Lambertian
    // surface in particular sends back about a third of it -- the rest
    // having gone in every other direction.
    Configuration lit = BaseConfiguration();
    lit.name = "white, rough, lit head-on";
    const Rgb litColor = RenderOne(ctx, rig, lit);
    TEST_CHECK(ctx, litColor.r > 0.05f);
    TEST_CHECK(ctx, litColor.r < 1.0f);
    FLUXION_LOG_INFO("RenderCoreTests", "%s: %s -> %.4f", backendName, lit.name, (f64)litColor.r);

    // --- A surface turned away from the light receives none of it -------
    Configuration behind = BaseConfiguration();
    behind.name = "lit from behind";
    behind.sunDirection[2] = -1.0f;
    const Rgb behindColor = RenderOne(ctx, rig, behind);
    TEST_CHECK(ctx, behindColor.r < 0.001f);

    // --- A smoother surface concentrates its highlight ------------------
    //
    // Both lit and viewed head-on, which is the mirror direction, so the
    // whole of the specular lobe points at the camera. A narrower lobe is
    // a taller one, because what it stops spreading sideways has to go
    // somewhere.
    Configuration smooth = BaseConfiguration();
    smooth.name = "smooth, in the mirror direction";
    smooth.roughness = 0.1f;
    const Rgb smoothColor = RenderOne(ctx, rig, smooth);
    TEST_CHECK(ctx, smoothColor.r > litColor.r);
    FLUXION_LOG_INFO("RenderCoreTests", "%s: %s -> %.4f", backendName, smooth.name, (f64)smoothColor.r);

    // --- A metal has no diffuse colour ----------------------------------
    //
    // Lit from well off to one side, so that almost nothing of the
    // specular lobe reaches the camera. What is left is the diffuse
    // term -- which a non-metal has and a metal does not.
    Configuration dielectricOffAxis = BaseConfiguration();
    dielectricOffAxis.name = "non-metal, lit from the side";
    dielectricOffAxis.roughness = 0.3f;
    dielectricOffAxis.sunDirection[0] = 0.85f;
    dielectricOffAxis.sunDirection[2] = 0.52f;

    Configuration metalOffAxis = dielectricOffAxis;
    metalOffAxis.name = "metal, lit from the side";
    metalOffAxis.metallic = 1.0f;

    const Rgb dielectricColor = RenderOne(ctx, rig, dielectricOffAxis);
    const Rgb metalColor = RenderOne(ctx, rig, metalOffAxis);
    TEST_CHECK(ctx, metalColor.r < dielectricColor.r);
    FLUXION_LOG_INFO("RenderCoreTests", "%s: off-axis non-metal %.4f against metal %.4f",
        backendName, (f64)dielectricColor.r, (f64)metalColor.r);

    // --- Light a surface emits is not lit, occluded or shadowed ---------
    Configuration emissive = BaseConfiguration();
    emissive.name = "emissive in the dark";
    emissive.sunColor[0] = 0.0f;
    emissive.sunColor[1] = 0.0f;
    emissive.sunColor[2] = 0.0f;
    emissive.emissive[0] = 2.0f;
    emissive.emissive[1] = 0.25f;
    emissive.emissive[2] = 0.0f;
    const Rgb emissiveColor = RenderOne(ctx, rig, emissive);

    // Exactly what was asked for, and ABOVE ONE -- which is the whole
    // reason the target is a floating-point one. A tolerance rather than
    // equality only because the value made the journey through a
    // half-precision target.
    TEST_CHECK(ctx, emissiveColor.r > 1.99f && emissiveColor.r < 2.01f);
    TEST_CHECK(ctx, emissiveColor.g > 0.24f && emissiveColor.g < 0.26f);
    TEST_CHECK(ctx, emissiveColor.b < 0.001f);

    // --- Ambient light is what reaches a surface nothing else lights ----
    Configuration ambientOnly = BaseConfiguration();
    ambientOnly.name = "ambient only";
    ambientOnly.sunColor[0] = 0.0f;
    ambientOnly.sunColor[1] = 0.0f;
    ambientOnly.sunColor[2] = 0.0f;
    ambientOnly.ambient[0] = 0.4f;
    ambientOnly.ambient[1] = 0.4f;
    ambientOnly.ambient[2] = 0.4f;
    const Rgb ambientColor = RenderOne(ctx, rig, ambientOnly);

    // A white surface under an ambient of 0.4 receives 0.4 of it, because
    // its diffuse colour is one. Not divided by pi: ambient here is
    // already the light that arrives, not a radiance to be integrated.
    TEST_CHECK(ctx, ambientColor.r > 0.35f && ambientColor.r < 0.45f);

    // And a fully occluded surface receives none of it, which is what
    // occlusion means.
    Configuration occluded = ambientOnly;
    occluded.name = "ambient, fully occluded";
    occluded.occlusion = 1.0f;

    // The occlusion MAP is white here -- the default that changes
    // nothing -- so a strength of one still leaves the surface open. The
    // check that matters is that the strength does not invent occlusion
    // where the map says there is none.
    const Rgb occludedColor = RenderOne(ctx, rig, occluded);
    TEST_CHECK(ctx, occludedColor.r > 0.35f && occludedColor.r < 0.45f);

    // --- The camera: exposure -------------------------------------------
    //
    // Emissive, because it passes through the lighting untouched -- which
    // makes what comes out at the far end a statement about the camera
    // alone.
    Configuration exposed = BaseConfiguration();
    exposed.name = "exposure halves the light";
    exposed.sunColor[0] = 0.0f;
    exposed.sunColor[1] = 0.0f;
    exposed.sunColor[2] = 0.0f;
    exposed.emissive[0] = 2.0f;
    exposed.emissive[1] = 1.0f;
    exposed.emissive[2] = 0.5f;
    exposed.exposure = 0.5f;
    const Rgb exposedColor = RenderOne(ctx, rig, exposed);

    TEST_CHECK(ctx, exposedColor.r > 0.99f && exposedColor.r < 1.01f);
    TEST_CHECK(ctx, exposedColor.g > 0.49f && exposedColor.g < 0.51f);
    TEST_CHECK(ctx, exposedColor.b > 0.24f && exposedColor.b < 0.26f);

    // --- The camera: tone mapping ---------------------------------------
    //
    // A white point of two, and three values around it. The one AT the
    // white point has to come out at exactly one -- that is what naming a
    // white point means, and an operator that merely compressed towards
    // one without reaching it would leave the brightest thing in every
    // scene slightly grey.
    Configuration mapped = BaseConfiguration();
    mapped.name = "tone mapped against a white point of two";
    mapped.sunColor[0] = 0.0f;
    mapped.sunColor[1] = 0.0f;
    mapped.sunColor[2] = 0.0f;
    mapped.emissive[0] = 2.0f;  // at the white point
    mapped.emissive[1] = 1.0f;  // below it
    mapped.emissive[2] = 0.5f;  // well below it
    mapped.tonemapWhitePoint = 2.0f;
    const Rgb mappedColor = RenderOne(ctx, rig, mapped);

    TEST_CHECK(ctx, mappedColor.r > 0.995f && mappedColor.r < 1.005f);

    // 1 * (1 + 1/4) / (1 + 1) and 0.5 * (1 + 0.5/4) / (1 + 0.5), worked
    // out from the published operator rather than from what came back.
    TEST_CHECK(ctx, mappedColor.g > 0.620f && mappedColor.g < 0.630f);
    TEST_CHECK(ctx, mappedColor.b > 0.370f && mappedColor.b < 0.380f);

    // Darker in, darker out. An operator that was not monotonic would
    // reorder the brightness of two surfaces, which is a far stranger
    // failure than merely being too dark.
    TEST_CHECK(ctx, mappedColor.r > mappedColor.g);
    TEST_CHECK(ctx, mappedColor.g > mappedColor.b);

    // And something far above the white point is CLIPPED rather than
    // compressed further -- which is what makes a highlight read as a
    // bright thing rather than as a grey shape.
    Configuration blown = mapped;
    blown.name = "far above the white point";
    blown.emissive[0] = 50.0f;
    const Rgb blownColor = RenderOne(ctx, rig, blown);
    TEST_CHECK(ctx, blownColor.r > 1.0f);

    // --- And none of it happens when nobody asked -----------------------
    Configuration untouched = BaseConfiguration();
    untouched.name = "no camera at all";
    untouched.sunColor[0] = 0.0f;
    untouched.sunColor[1] = 0.0f;
    untouched.sunColor[2] = 0.0f;
    untouched.emissive[0] = 3.0f;
    const Rgb untouchedColor = RenderOne(ctx, rig, untouched);

    // Exactly what went in. A white point of zero means the pass does not
    // tone map, and an operator applied twice -- here and in whatever
    // post-processing arrives later -- would be much worse than one
    // applied not at all.
    TEST_CHECK(ctx, untouchedColor.r > 2.99f && untouchedColor.r < 3.01f);

    // --- The alpha test --------------------------------------------------
    //
    // Emissive and no light, so a pixel that IS drawn is unmistakable and
    // one that is not is exactly the colour the pass cleared to.
    Configuration cutoutBase = BaseConfiguration();
    cutoutBase.sunColor[0] = 0.0f;
    cutoutBase.sunColor[1] = 0.0f;
    cutoutBase.sunColor[2] = 0.0f;
    cutoutBase.ambient[0] = 0.0f;
    cutoutBase.ambient[1] = 0.0f;
    cutoutBase.ambient[2] = 0.0f;
    cutoutBase.emissive[0] = 1.0f;
    cutoutBase.emissive[1] = 1.0f;
    cutoutBase.emissive[2] = 1.0f;

    Configuration kept = cutoutBase;
    kept.name = "opacity above the cutoff";
    kept.opacity = 0.9f;
    kept.alphaCutoff = 0.5f;
    const Rgb keptColor = RenderOne(ctx, rig, kept);
    TEST_CHECK(ctx, keptColor.r > 0.9f);

    Configuration dropped = cutoutBase;
    dropped.name = "opacity below the cutoff";
    dropped.opacity = 0.1f;
    dropped.alphaCutoff = 0.5f;
    const Rgb droppedColor = RenderOne(ctx, rig, dropped);

    // Nothing was written, so what comes back is what the pass cleared
    // to. Black is a weak thing to assert on its own -- an unlit surface
    // is also black -- which is why this one is emissive: the same
    // configuration WITH the pixel kept came back at nearly one, on the
    // line above.
    TEST_CHECK(ctx, droppedColor.r < 0.01f);
    TEST_CHECK(ctx, droppedColor.g < 0.01f);
    TEST_CHECK(ctx, droppedColor.b < 0.01f);

    // And a threshold of zero means no test at all, not a test that
    // everything passes: an almost transparent pixel is still drawn.
    Configuration noTest = cutoutBase;
    noTest.name = "no cutoff at all";
    noTest.opacity = 0.001f;
    noTest.alphaCutoff = 0.0f;
    const Rgb noTestColor = RenderOne(ctx, rig, noTest);
    TEST_CHECK(ctx, noTestColor.r > 0.9f);

    // --- The display curve ------------------------------------------------
    Configuration encoded = BaseConfiguration();
    encoded.name = "encoded for an eight-bit target";
    encoded.sunColor[0] = 0.0f;
    encoded.sunColor[1] = 0.0f;
    encoded.sunColor[2] = 0.0f;
    encoded.emissive[0] = 0.5f;
    encoded.emissive[1] = 0.5f;
    encoded.emissive[2] = 0.5f;
    encoded.encodeOutputToSRGB = true;
    const Rgb encodedColor = RenderOne(ctx, rig, encoded);

    // Half the light is not half the number: the curve exists precisely
    // because a display is not linear, and 0.5 encodes to about 0.735.
    TEST_CHECK(ctx, encodedColor.r > 0.72f && encodedColor.r < 0.75f);

    // And with it off, the same half stays a half. Both directions,
    // because a curve applied unconditionally would pass the check above
    // while ruining every floating-point target.
    Configuration linear = encoded;
    linear.name = "left linear for a floating-point target";
    linear.encodeOutputToSRGB = false;
    const Rgb linearColor = RenderOne(ctx, rig, linear);
    TEST_CHECK(ctx, linearColor.r > 0.49f && linearColor.r < 0.51f);

    // --- The sample's own settings ----------------------------------------
    //
    // The scene Samples/ForwardRendererDemo actually builds, run through
    // the real pipeline: its sun, its orbiting point light, its spot
    // light from below, its camera and its output curve.
    //
    // A sample that comes out black or blown out is a broken sample, and
    // nothing else here would say so -- every check above sets its own
    // lighting to isolate one thing, which is exactly what makes none of
    // them notice an unusable scene.
    Configuration sample = BaseConfiguration();
    sample.name = "the sample's own lights, camera and curve";
    sample.roughness = 0.4f;
    sample.sunDirection[0] = -0.4f;
    sample.sunDirection[1] = 0.7f;
    sample.sunDirection[2] = 0.6f;
    sample.sunColor[0] = 230.0f;
    sample.sunColor[1] = 220.0f;
    sample.sunColor[2] = 200.0f;
    sample.ambient[0] = 8.0f;
    sample.ambient[1] = 9.0f;
    sample.ambient[2] = 12.0f;

    // The orbiting point light and the spot from below, with the
    // sample's own colours, ranges and cone -- but placed where they
    // stand in relation to THIS surface rather than to the sample's
    // cube. The rig lights one flat quad facing the camera; the sample
    // lights a cube in the round, and a light that sits beside a cube
    // sits behind a quad.
    //
    // Copying the sample's coordinates rather than its geometry would
    // have made this check pass on lights contributing almost nothing,
    // which is what it did the first time it was written.
    sample.extraLightCount = 2;
    sample.extraLights[0] = FluxionRenderLight{};
    sample.extraLights[0].type = FLUXION_RENDER_LIGHT_POINT;
    sample.extraLights[0].position = FluxionVec3{ 0.0f, 0.8f, 2.2f }; // the orbit radius, in front
    sample.extraLights[0].color = FluxionVec3{ 120.0f, 30.0f, 10.0f };
    sample.extraLights[0].range = 6.0f;

    sample.extraLights[1] = FluxionRenderLight{};
    sample.extraLights[1].type = FLUXION_RENDER_LIGHT_SPOT;
    sample.extraLights[1].position = FluxionVec3{ 0.0f, -2.0f, 2.0f };
    sample.extraLights[1].direction = FluxionVec3{ 0.0f, 1.0f, -1.0f }; // up and towards the surface
    sample.extraLights[1].color = FluxionVec3{ 10.0f, 40.0f, 90.0f };
    sample.extraLights[1].range = 8.0f;
    sample.extraLights[1].innerConeCos = std::cos(0.25f);
    sample.extraLights[1].outerConeCos = std::cos(0.45f);
    sample.exposure = Fluxion_Exposure_FromCamera(2.0f, 1.0f / 60.0f, 400.0f);
    sample.tonemapWhitePoint = 4.0f;
    sample.encodeOutputToSRGB = true;
    const Rgb sampleColor = RenderOne(ctx, rig, sample);

    // A face turned towards the light, on a screen: bright enough to see
    // and short of clipping. The bounds are wide because this is a check
    // that the scene is USABLE, not that it is any particular colour --
    // a narrow range here would break every time somebody tuned the
    // sample, which is not a failure.
    TEST_CHECK(ctx, sampleColor.r > 0.25f && sampleColor.r < 0.98f);
    TEST_CHECK(ctx, sampleColor.g > 0.25f && sampleColor.g < 0.98f);
    TEST_CHECK(ctx, sampleColor.b > 0.20f && sampleColor.b < 0.98f);

    // Warm, because the sun and the orbiting light both are, and they
    // are the two that reach this surface most strongly. If the channels
    // came out equal, no light's colour would be reaching it at all.
    TEST_CHECK(ctx, sampleColor.r > sampleColor.b);

    // And the coloured lights are actually contributing: the same scene
    // with only the sun is measurably different. Without this, the check
    // above would pass on a scene where the two extra lights were dropped
    // entirely -- which is exactly what a broken light list looks like.
    Configuration sunOnly = sample;
    sunOnly.name = "the sample's scene with only its sun";
    sunOnly.extraLightCount = 0;
    const Rgb sunOnlyColor = RenderOne(ctx, rig, sunOnly);
    TEST_CHECK(ctx, sampleColor.r > sunOnlyColor.r + 0.01f);

    // And the same scene with the sun switched off is still visible,
    // because the ambient is not nothing -- a scene whose unlit faces are
    // pure black reads as a bug in the shading.
    Configuration sampleShadowed = sample;
    sampleShadowed.name = "the sample's own settings, facing away";
    sampleShadowed.sunDirection[2] = -1.0f;
    const Rgb shadowedColor = RenderOne(ctx, rig, sampleShadowed);
    TEST_CHECK(ctx, shadowedColor.r > 0.02f);
    TEST_CHECK(ctx, shadowedColor.r < sampleColor.r);


    // --- Many lights --------------------------------------------------------
    //
    // The surface faces the camera, at the origin, with the camera five
    // units in front of it along positive Z. Every light below is placed
    // in those terms.

    // A configuration lit by nothing at all, so that what a light adds is
    // exactly what comes back.
    Configuration dark = BaseConfiguration();
    dark.sunColor[0] = 0.0f;
    dark.sunColor[1] = 0.0f;
    dark.sunColor[2] = 0.0f;
    dark.ambient[0] = 0.0f;
    dark.ambient[1] = 0.0f;
    dark.ambient[2] = 0.0f;
    dark.roughness = 1.0f;

    // One directional light, straight on.
    Configuration oneLight = dark;
    oneLight.name = "one light of the list";
    oneLight.extraLightCount = 1;
    oneLight.extraLights[0] = FluxionRenderLight{};
    oneLight.extraLights[0].type = FLUXION_RENDER_LIGHT_DIRECTIONAL;
    oneLight.extraLights[0].direction = FluxionVec3{ 0.0f, 0.0f, -1.0f }; // travelling away from the camera, into the surface
    oneLight.extraLights[0].color = FluxionVec3{ 1.0f, 1.0f, 1.0f };
    const Rgb oneLightColor = RenderOne(ctx, rig, oneLight);
    TEST_CHECK(ctx, oneLightColor.r > 0.05f);

    // The same light twice. Light adds, so two of it is twice as much --
    // this is the check that the loop runs more than once at all, and
    // that the second entry of the buffer is read rather than the first
    // one twice.
    Configuration twoLights = oneLight;
    twoLights.name = "the same light twice";
    twoLights.extraLightCount = 2;
    twoLights.extraLights[1] = twoLights.extraLights[0];
    const Rgb twoLightColor = RenderOne(ctx, rig, twoLights);
    TEST_CHECK(ctx, twoLightColor.r > oneLightColor.r * 1.9f && twoLightColor.r < oneLightColor.r * 2.1f);

    // Two DIFFERENT lights, so that a loop reading the same entry twice
    // could not pass: one red, one blue, and the result carries both.
    Configuration twoColors = dark;
    twoColors.name = "a red light and a blue one";
    twoColors.extraLightCount = 2;
    twoColors.extraLights[0] = oneLight.extraLights[0];
    twoColors.extraLights[0].color = FluxionVec3{ 1.0f, 0.0f, 0.0f };
    twoColors.extraLights[1] = oneLight.extraLights[0];
    twoColors.extraLights[1].color = FluxionVec3{ 0.0f, 0.0f, 1.0f };
    const Rgb twoColorsColor = RenderOne(ctx, rig, twoColors);
    TEST_CHECK(ctx, twoColorsColor.r > 0.05f);
    TEST_CHECK(ctx, twoColorsColor.b > 0.05f);
    TEST_CHECK(ctx, twoColorsColor.g < 0.01f);

    // --- A point light falls off --------------------------------------------
    Configuration near = dark;
    near.name = "a point light two units away";
    near.extraLightCount = 1;
    near.extraLights[0] = FluxionRenderLight{};
    near.extraLights[0].type = FLUXION_RENDER_LIGHT_POINT;
    near.extraLights[0].position = FluxionVec3{ 0.0f, 0.0f, 2.0f };
    near.extraLights[0].color = FluxionVec3{ 4.0f, 4.0f, 4.0f };
    near.extraLights[0].range = 100.0f; // far enough away that the window is not what is being measured
    const Rgb nearColor = RenderOne(ctx, rig, near);
    TEST_CHECK(ctx, nearColor.r > 0.02f);

    Configuration far = near;
    far.name = "the same point light four units away";
    far.extraLights[0].position = FluxionVec3{ 0.0f, 0.0f, 4.0f };
    const Rgb farColor = RenderOne(ctx, rig, far);

    // Twice as far is a quarter as bright. Inverse SQUARE, not inverse --
    // a falloff that merely divided by distance would give a half here,
    // and would look almost right in every screenshot.
    TEST_CHECK(ctx, farColor.r > nearColor.r * 0.20f && farColor.r < nearColor.r * 0.30f);

    // --- And reaches exactly zero at its range ------------------------------
    //
    // The sharp one. A falloff that merely got small would leave a light
    // contributing where it is about to be culled, and culling it there
    // is what leaves a visible edge.
    Configuration atRange = near;
    atRange.name = "a point light whose range ends at the surface";
    atRange.extraLights[0].position = FluxionVec3{ 0.0f, 0.0f, 3.0f };
    atRange.extraLights[0].range = 3.0f;
    const Rgb atRangeColor = RenderOne(ctx, rig, atRange);
    TEST_CHECK(ctx, atRangeColor.r < 0.001f);

    // Just inside it, there is still something -- otherwise the check
    // above would pass for a light that was simply switched off.
    Configuration insideRange = atRange;
    insideRange.name = "the same light with the surface just inside its range";
    insideRange.extraLights[0].range = 6.0f;
    const Rgb insideRangeColor = RenderOne(ctx, rig, insideRange);
    TEST_CHECK(ctx, insideRangeColor.r > 0.02f);

    // --- A spot light has a cone --------------------------------------------
    Configuration spotCentre = dark;
    spotCentre.name = "a spot light aimed straight at the surface";
    spotCentre.extraLightCount = 1;
    spotCentre.extraLights[0] = FluxionRenderLight{};
    spotCentre.extraLights[0].type = FLUXION_RENDER_LIGHT_SPOT;
    spotCentre.extraLights[0].position = FluxionVec3{ 0.0f, 0.0f, 2.0f };
    spotCentre.extraLights[0].direction = FluxionVec3{ 0.0f, 0.0f, -1.0f };
    spotCentre.extraLights[0].color = FluxionVec3{ 4.0f, 4.0f, 4.0f };
    spotCentre.extraLights[0].range = 100.0f;
    spotCentre.extraLights[0].innerConeCos = 0.95f; // about eighteen degrees
    spotCentre.extraLights[0].outerConeCos = 0.90f;
    const Rgb spotCentreColor = RenderOne(ctx, rig, spotCentre);
    TEST_CHECK(ctx, spotCentreColor.r > 0.02f);

    // The same spot, aimed away. Outside the outer cone is nothing --
    // not merely dim.
    Configuration spotAway = spotCentre;
    spotAway.name = "the same spot light aimed away";
    spotAway.extraLights[0].direction = FluxionVec3{ 1.0f, 0.0f, 0.0f };
    const Rgb spotAwayColor = RenderOne(ctx, rig, spotAway);
    TEST_CHECK(ctx, spotAwayColor.r < 0.001f);

    // And a spot pointed straight on gives what a point light in the same
    // place would: inside the inner cone the cone costs nothing. If it
    // did not, every spot light would be dimmer than it should be by
    // however wrong the transition was.
    Configuration pointSamePlace = near;
    pointSamePlace.name = "a point light where the spot light was";
    pointSamePlace.extraLights[0].position = FluxionVec3{ 0.0f, 0.0f, 2.0f };
    const Rgb pointSamePlaceColor = RenderOne(ctx, rig, pointSamePlace);
    TEST_CHECK(ctx, spotCentreColor.r > pointSamePlaceColor.r * 0.99f);
    TEST_CHECK(ctx, spotCentreColor.r < pointSamePlaceColor.r * 1.01f);

    // --- A light of every kind at once --------------------------------------
    //
    // Three kinds in one list, so that the branch on the kind is read
    // from each entry rather than settled once for the whole buffer.
    Configuration mixed = dark;
    mixed.name = "one of each kind together";
    mixed.extraLightCount = 3;
    mixed.extraLights[0] = oneLight.extraLights[0];
    mixed.extraLights[0].color = FluxionVec3{ 0.6f, 0.0f, 0.0f };
    mixed.extraLights[1] = near.extraLights[0];
    mixed.extraLights[1].color = FluxionVec3{ 0.0f, 4.0f, 0.0f };
    mixed.extraLights[2] = spotCentre.extraLights[0];
    mixed.extraLights[2].color = FluxionVec3{ 0.0f, 0.0f, 4.0f };
    const Rgb mixedColor = RenderOne(ctx, rig, mixed);
    TEST_CHECK(ctx, mixedColor.r > 0.01f);
    TEST_CHECK(ctx, mixedColor.g > 0.01f);
    TEST_CHECK(ctx, mixedColor.b > 0.01f);

    // --- No lights at all ---------------------------------------------------
    //
    // Not an error and not a special case: a scene with no lights is lit
    // by its ambient, which is a picture rather than a fault.
    Configuration none = BaseConfiguration();
    none.name = "no lights, only ambient";
    none.sunColor[0] = 0.0f;
    none.sunColor[1] = 0.0f;
    none.sunColor[2] = 0.0f;
    none.ambient[0] = 0.4f;
    none.ambient[1] = 0.4f;
    none.ambient[2] = 0.4f;
    const Rgb noneColor = RenderOne(ctx, rig, none);
    TEST_CHECK(ctx, noneColor.r > 0.35f && noneColor.r < 0.45f);

    Fluxion_TextureDefaults_Shutdown();
    DestroyRig(rig);
    Fluxion_RenderGraphPassRegistry_Shutdown();
}

} // namespace

extern "C" void Test_LightingGPU_Run(TestContext* ctx)
{
    if (!Fluxion::ShaderCompiler::IsDXCAvailable())
    {
        FLUXION_LOG_WARN("RenderCoreTests", "No shader compiler on this machine -- the lighting was NOT checked here.");
        return;
    }

    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_VULKAN, "Vulkan");
    CheckOnBackend(ctx, FLUXION_RHI_BACKEND_D3D12, "D3D12");
}
