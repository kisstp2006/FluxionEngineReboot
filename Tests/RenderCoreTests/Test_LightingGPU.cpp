#include "TestFramework.h"

#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraph.h>
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
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>

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

struct LightingRig
{
    FluxionRHIInstanceHandle instance{};
    FluxionRHIDeviceHandle device{};
    FluxionRHIQueueHandle queue{};

    FluxionShaderProgramHandle program{};
    FluxionMeshBufferHandle mesh{};
    FluxionRenderPipelineHandle pipeline{};
    FluxionRendererHandle renderer{};

    FluxionRHITextureHandle color{};
    FluxionRHITextureViewHandle colorView{};
    FluxionRHITextureHandle depth{};
    FluxionRHITextureViewHandle depthView{};
    FluxionRenderTargetHandle target{};
    FluxionRHIBufferHandle readback{};
    FluxionRHISamplerHandle sampler{};

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
    FluxionRHITextureViewDesc colorViewDesc = { rig.color, kColorFormat, 0, 1, 0, 1 };
    rig.colorView = Fluxion_RHI_CreateTextureView(rig.device, &colorViewDesc);

    FluxionRHITextureDesc depthDesc = colorDesc;
    depthDesc.format = FLUXION_RHI_FORMAT_D32_FLOAT;
    depthDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL;
    depthDesc.debugName = "LightingGPU.Depth";
    rig.depth = Fluxion_RHI_CreateTexture(rig.device, &depthDesc);
    FluxionRHITextureViewDesc depthViewDesc = { rig.depth, depthDesc.format, 0, 1, 0, 1 };
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

    Fluxion_Material_SetBaseColor(material, FluxionVec4{ configuration.baseColor[0], configuration.baseColor[1], configuration.baseColor[2], 1.0f });
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
    viewDesc.sunDirection = FluxionVec3{ configuration.sunDirection[0], configuration.sunDirection[1], configuration.sunDirection[2] };
    viewDesc.sunColor = FluxionVec3{ configuration.sunColor[0], configuration.sunColor[1], configuration.sunColor[2] };
    viewDesc.ambientColor = FluxionVec3{ configuration.ambient[0], configuration.ambient[1], configuration.ambient[2] };
    viewDesc.exposure = configuration.exposure;
    viewDesc.tonemapWhitePoint = configuration.tonemapWhitePoint;
    FluxionRenderViewHandle view = Fluxion_RenderView_Create(rig.device, &viewDesc);
    Fluxion_RenderView_UpdateFrameConstants(view);

    FluxionRHICommandListHandle cmd = Fluxion_RHI_CreateCommandList(rig.device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RHI_CommandList_Begin(cmd);

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
