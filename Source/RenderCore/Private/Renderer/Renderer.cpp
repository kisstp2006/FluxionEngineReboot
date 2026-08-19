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

// C++ only for the debug-draw pipeline's shader compile step (dxc's
// HLSL-text -> SPIR-V-bytes adapter is a Fluxion::ShaderCompiler C++
// API) -- everything else here is plain struct bookkeeping that would
// have been just as happy in C.

#include <Fluxion/RenderCore/Renderer/Renderer.h>

#include "RendererInternal.h"

#include <Fluxion/RenderCore/Scene/RenderWorld.h>

#include <Fluxion/Core/Diagnostics/ProfileScope.hpp>
#include <Fluxion/Foundation/Memory/MemoryTracker.h>

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>

#include <cstring>

namespace
{

FluxionRenderer s_renderers[FLUXION_RENDERER_MAX_INSTANCES];

FluxionRenderer* Resolve(FluxionRendererHandle handle)
{
    if (handle.index >= FLUXION_RENDERER_MAX_INSTANCES) return nullptr;
    FluxionRenderer* renderer = &s_renderers[handle.index];
    if (!renderer->alive || renderer->generation != handle.generation) return nullptr;
    return renderer;
}

// Self-contained: raw HLSL text compiled straight through dxc, bypassing
// this engine's own .jsl language/ShaderCompiler front end and the
// Material/ShaderProgram machinery entirely -- appropriate for something
// this small (see DebugDraw.h's comment), where fighting that machinery
// for two triangles' worth of shading would cost more than it saves.
// `[[vk::binding(0, 1)]]`/`space1` line up with FluxionRendererInternal_MakeFrameLayoutDesc's
// FRAME group (binding 0 of bind-group-frequency index 1).
const char* kDebugVertexHLSL = R"(
[[vk::binding(0, 1)]] cbuffer FrameConstants : register(b0, space1)
{
    float4x4 viewProjection;
};

// TEXCOORD0/TEXCOORD1, not the more idiomatic-looking POSITION0/COLOR0:
// the D3D12 backend's vertex input layout hardcodes SemanticName =
// "TEXCOORD" for every attribute (see D3D12Pipeline.cpp), matching the
// .jsl HLSL backend's own fixed convention -- this hand-written shader
// bypasses that backend but still has to match its D3D12 input-layout
// assumption to link.
struct VSInput
{
    float3 position : TEXCOORD0;
    float4 color : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(viewProjection, float4(input.position, 1.0));
    output.color = input.color;
    return output;
}
)";

const char* kDebugFragmentHLSL = R"(
struct PSInput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

float4 main(PSInput input) : SV_Target0
{
    return input.color;
}
)";

// GLSL twins of the HLSL sources above, for the OpenGL backend --
// Fluxion_RHI_CreateShader's bytecode field carries raw GLSL source text
// for that backend (same convention as ShaderProgram.cpp's CompileStage),
// not SPIR-V/DXIL bytecode, and this hand-written HLSL never goes through
// Fluxion::ShaderCompiler::Compile()'s .jsl front end (which is what
// would otherwise produce a glslSource twin automatically) -- so a
// companion GLSL string is authored by hand here instead. `layout(binding
// = 16)` matches FLUXION_RHIOPENGL_BINDINGS_PER_GROUP (16) *
// FLUXION_RHI_BIND_GROUP_FRAME (1) + binding 0, the same flat-binding
// scheme OpenGLBinding.cpp's CommandListSetBindGroup and the .jsl
// compiler's own GLSLBackend.cpp both use.
const char* kDebugVertexGLSL = R"(#version 450 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 vColor;

layout(binding = 16) uniform GroupFrameBlock
{
    mat4 viewProjection;
};

void main()
{
    gl_Position = viewProjection * vec4(position, 1.0);
    vColor = color;
}
)";

const char* kDebugFragmentGLSL = R"(#version 450 core
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vColor;
}
)";

bool CompileDebugStage(FluxionRHIDeviceHandle device, FluxionRHIBackendType backend, const char* hlslSource, const char* glslSource, Fluxion::ShaderCompiler::ShaderStage stage, FluxionRHIShaderHandle* outShader)
{
    FluxionRHIShaderStage rhiStage = (stage == Fluxion::ShaderCompiler::ShaderStage::Vertex) ? FLUXION_RHI_SHADER_STAGE_VERTEX : FLUXION_RHI_SHADER_STAGE_FRAGMENT;

    if (backend == FLUXION_RHI_BACKEND_OPENGL)
    {
        FluxionRHIShaderDesc desc;
        desc.stage = rhiStage;
        desc.bytecode = glslSource;
        desc.bytecodeSize = std::strlen(glslSource);
        desc.entryPoint = "main";
        desc.debugName = "Fluxion.Renderer.DebugDraw";

        *outShader = Fluxion_RHI_CreateShader(device, &desc);
        return FLUXION_HANDLE_IS_VALID(*outShader);
    }

    Fluxion::ShaderCompiler::DiagnosticList diagnostics;

    if (backend == FLUXION_RHI_BACKEND_D3D12)
    {
        auto dxil = Fluxion::ShaderCompiler::CompileToDxil(hlslSource, stage, "main", diagnostics);
        if (!dxil.IsOk())
        {
            for (const auto& d : diagnostics.entries) FLUXION_LOG_ERROR("Renderer", "dxc (debug draw): %s", d.message.c_str());
            return false;
        }

        FluxionRHIShaderDesc desc;
        desc.stage = rhiStage;
        desc.bytecode = dxil.Value().data();
        desc.bytecodeSize = dxil.Value().size();
        desc.entryPoint = "main";
        desc.debugName = "Fluxion.Renderer.DebugDraw";

        *outShader = Fluxion_RHI_CreateShader(device, &desc);
        return FLUXION_HANDLE_IS_VALID(*outShader);
    }

    // Vulkan, Null, and anything else: HLSL text -> dxc -> SPIR-V.
    auto spirv = Fluxion::ShaderCompiler::CompileToSpirv(hlslSource, stage, "main", diagnostics);
    if (!spirv.IsOk())
    {
        for (const auto& d : diagnostics.entries) FLUXION_LOG_ERROR("Renderer", "dxc (debug draw): %s", d.message.c_str());
        return false;
    }

    FluxionRHIShaderDesc desc;
    desc.stage = rhiStage;
    desc.bytecode = spirv.Value().data();
    desc.bytecodeSize = spirv.Value().size();
    desc.entryPoint = "main";
    desc.debugName = "Fluxion.Renderer.DebugDraw";

    *outShader = Fluxion_RHI_CreateShader(device, &desc);
    return FLUXION_HANDLE_IS_VALID(*outShader);
}

// Only the pipeline object, built against whatever colour format the
// renderer has been told it draws into. Split out from the rest because
// that format can change after the renderer exists -- a texture view
// carries no queryable format, so it is the caller who says what it is,
// and saying a different one has to rebuild this and nothing else.
void CreateDebugDrawPipelineObject(FluxionRenderer& renderer)
{
    if (!FLUXION_HANDLE_IS_VALID(renderer.debugVertexShader) || !FLUXION_HANDLE_IS_VALID(renderer.debugFragmentShader)) return;

    FluxionRHIVertexLayout vertexLayout{};
    vertexLayout.attributes[0].location = 0;
    vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    vertexLayout.attributes[0].offset = offsetof(FluxionDebugDrawVertex, position);
    vertexLayout.attributes[1].location = 1;
    vertexLayout.attributes[1].format = FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT;
    vertexLayout.attributes[1].offset = offsetof(FluxionDebugDrawVertex, color);
    vertexLayout.attributeCount = 2;
    vertexLayout.stride = sizeof(FluxionDebugDrawVertex);

    FluxionRHIGraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = renderer.debugVertexShader;
    pipelineDesc.fragmentShader = renderer.debugFragmentShader;
    pipelineDesc.vertexLayout = vertexLayout;
    pipelineDesc.rasterState.cullMode = FLUXION_RHI_CULL_MODE_NONE;

    // Tested against the scene's depth once the caller has said what that
    // depth is, so a line behind something is behind it. Never written,
    // though: these are marks laid over a scene, and a mark that pushed
    // the depth around would start hiding the very thing it is pointing
    // at. Without a stated depth target there is nothing to test against,
    // and it all draws over the top.
    const bool testAgainstDepth = renderer.attachmentDepthFormat != FLUXION_RHI_FORMAT_UNKNOWN;
    pipelineDesc.depthState.testEnable = testAgainstDepth;
    pipelineDesc.depthState.compareOp = FLUXION_RHI_COMPARE_OP_LESS_OR_EQUAL;
    pipelineDesc.depthState.writeEnable = false;
    pipelineDesc.blendState.blendEnable = true;
    pipelineDesc.topology = FLUXION_RHI_PRIMITIVE_TOPOLOGY_LINE_LIST; // triangles are appended as their own 3 vertices too -- see DebugDraw.c
    pipelineDesc.colorFormats[0] = renderer.attachmentColorFormat;
    pipelineDesc.colorFormatCount = 1;
    pipelineDesc.depthFormat = renderer.attachmentDepthFormat;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_FRAME] = renderer.debugFrameBindGroupLayout;
    pipelineDesc.bindGroupLayoutCount = FLUXION_RHI_BIND_GROUP_FRAME + 1;
    pipelineDesc.debugName = "Fluxion.Renderer.DebugDraw.Pipeline";
    renderer.debugPipeline = Fluxion_RHI_CreateGraphicsPipeline(renderer.device, &pipelineDesc);
}

// Everything the debug-draw pipeline is built out of, but not the
// pipeline itself -- see the comment at the end of this function.
void CreateDebugDrawResources(FluxionRenderer& renderer)
{
    FluxionRHIBackendType backend = Fluxion_RHI_GetDeviceBackendType(renderer.device);
    bool ok = CompileDebugStage(renderer.device, backend, kDebugVertexHLSL, kDebugVertexGLSL, Fluxion::ShaderCompiler::ShaderStage::Vertex, &renderer.debugVertexShader);
    ok = ok && CompileDebugStage(renderer.device, backend, kDebugFragmentHLSL, kDebugFragmentGLSL, Fluxion::ShaderCompiler::ShaderStage::Fragment, &renderer.debugFragmentShader);
    if (!ok) return;

    FluxionRHIBufferDesc vertexBufferDesc;
    vertexBufferDesc.size = (usize)FLUXION_RENDERER_MAX_DEBUG_VERTICES * sizeof(FluxionDebugDrawVertex);
    vertexBufferDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_VERTEX_BUFFER;
    vertexBufferDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    vertexBufferDesc.debugName = "Fluxion.Renderer.DebugDraw.VertexBuffer";
    renderer.debugVertexBuffer = Fluxion_RHI_CreateBuffer(renderer.device, &vertexBufferDesc);
    if (FLUXION_HANDLE_IS_VALID(renderer.debugVertexBuffer)) FluxionRendererInternal_RecordGpuAlloc(false, vertexBufferDesc.size);

    FluxionRHIBindGroupLayoutDesc frameLayoutDesc = FluxionRendererInternal_MakeFrameLayoutDesc();
    renderer.debugFrameBindGroupLayout = Fluxion_RHI_CreateBindGroupLayout(renderer.device, &frameLayoutDesc);

    // The pipeline itself waits: it has to be built against the colour
    // format it will draw into, and a caller that is going to say what
    // that is has not had the chance yet. It is built the first time
    // anything is actually drawn, by which time the answer is settled --
    // and a program that never draws a debug line never builds one at
    // all.
}

} // namespace

extern "C" void FluxionRendererInternal_EnsureMemoryDomains(void)
{
    // Nothing to do when no host initialised the tracker -- statistics
    // are an offer, not a requirement, and registering would assert.
    if (!Fluxion_MemoryTracker_IsInitialized()) return;

    static bool s_registered = false;
    if (s_registered) return;
    s_registered = true;

    FluxionMemoryDomainDesc rendererDomain = {};
    rendererDomain.id = FLUXION_MEMORY_DOMAIN_ID_OF(Renderer);
    rendererDomain.name = "Renderer";
    rendererDomain.parent = FLUXION_MEMORY_DOMAIN_ID_INVALID;
    Fluxion_MemoryTracker_RegisterDomain(&rendererDomain);

    // A child, so upload bytes both stand on their own and roll up into
    // the renderer's total.
    FluxionMemoryDomainDesc uploadDomain = {};
    uploadDomain.id = FLUXION_MEMORY_DOMAIN_ID_OF(GPUUpload);
    uploadDomain.name = "GPUUpload";
    uploadDomain.parent = rendererDomain.id;
    Fluxion_MemoryTracker_RegisterDomain(&uploadDomain);
}

extern "C" void FluxionRendererInternal_RecordGpuAlloc(bool upload, usize bytes)
{
    if (!Fluxion_MemoryTracker_IsInitialized()) return;
    FluxionRendererInternal_EnsureMemoryDomains();
    Fluxion_MemoryTracker_RecordAlloc(upload ? FLUXION_MEMORY_DOMAIN_ID_OF(GPUUpload) : FLUXION_MEMORY_DOMAIN_ID_OF(Renderer), bytes);
}

extern "C" void FluxionRendererInternal_RecordGpuFree(bool upload, usize bytes)
{
    if (!Fluxion_MemoryTracker_IsInitialized()) return;
    FluxionRendererInternal_EnsureMemoryDomains();
    Fluxion_MemoryTracker_RecordFree(upload ? FLUXION_MEMORY_DOMAIN_ID_OF(GPUUpload) : FLUXION_MEMORY_DOMAIN_ID_OF(Renderer), bytes);
}

extern "C" FluxionRendererHandle Fluxion_Renderer_Create(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue)
{
    FluxionRendererHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    u32 index = FLUXION_RENDERER_MAX_INSTANCES;
    for (u32 i = 0; i < FLUXION_RENDERER_MAX_INSTANCES; ++i)
    {
        if (!s_renderers[i].alive) { index = i; break; }
    }
    if (index == FLUXION_RENDERER_MAX_INSTANCES)
    {
        FLUXION_LOG_ERROR("Renderer", "Exceeded FLUXION_RENDERER_MAX_INSTANCES");
        return invalid;
    }

    FluxionRenderer* renderer = &s_renderers[index];
    u32 generation = renderer->generation;
    std::memset(renderer, 0, sizeof(*renderer));
    renderer->alive = true;
    renderer->generation = generation;
    renderer->device = device;
    renderer->queue = queue;

    // A zero-filled handle ({index:0, generation:0}) is NOT the same as
    // an invalid one ({index:FLUXION_HANDLE_INVALID_INDEX, generation:0})
    // -- FLUXION_HANDLE_IS_VALID only checks index != INVALID_INDEX, so a
    // plain memset above leaves every handle field looking like a
    // *valid* handle pointing at whatever real object happens to occupy
    // that RHI pool's slot 0. Every field CreateDebugDrawResources might
    // leave untouched on a dxc/compile failure (debugVertexBuffer,
    // debugVertexShader, debugFragmentShader, debugPipeline,
    // debugFrameBindGroupLayout) needs the real invalid sentinel here
    // too, not just the object list/currentView, or a later Destroy call
    // would try to free a bogus "valid" handle instead of skipping it.
    renderer->currentView = FluxionRenderViewHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->gpuScene = FluxionGPUSceneHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->cullMode = FLUXION_RENDERER_CULL_ON_HOST;

    // What survives from one frame to the next, and what draws into it.
    // The same sentinel for the same reason -- and this one was measured:
    // a zeroed history handle looks like TEXTURE ZERO, so the first frame
    // "remade" the history by destroying whatever really occupied that
    // slot. In the sample that was the material's base colour map, and
    // what it looked like was a material whose descriptor had never been
    // written -- four steps away from anything to do with a history.
    renderer->history.motionTexture = FluxionRHITextureHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->history.motionView = FluxionRHITextureViewHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->motionProgram = FluxionShaderProgramHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->motionPipeline = FluxionRHIPipelineHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->motionFrameLayout = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->history.pyramidTexture = FluxionRHITextureHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->pyramidProgram = FluxionShaderProgramHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->pyramidPipeline = FluxionRHIPipelineHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->pyramidLayout = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->pyramidSampler = FluxionRHISamplerHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->pyramidVertexBuffer = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->pyramidUniformBuffer = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->pyramidBoundDepthView = FluxionRHITextureViewHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    for (u32 i = 0; i < FLUXION_RENDER_HISTORY_MAX_PYRAMID_LEVELS; ++i)
    {
        renderer->history.pyramidTargetViews[i] = FluxionRHITextureViewHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
        renderer->history.pyramidSampleViews[i] = FluxionRHITextureViewHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
        renderer->history.pyramidWholeView = FluxionRHITextureViewHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
        renderer->pyramidBindGroups[i] = FluxionRHIBindGroupHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }
    renderer->debugVertexBuffer = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->debugVertexShader = FluxionRHIShaderHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->debugFragmentShader = FluxionRHIShaderHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->debugPipeline = FluxionRHIPipelineHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->debugFrameBindGroupLayout = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };

    // The sky's three, for exactly the reason above -- and this one was
    // not hypothetical. Left zeroed, the program handle read as valid, so
    // the sky was never built; the pipeline handle then read as valid
    // too, and pointed at whatever pipeline happened to hold slot zero.
    // The sky was drawn with somebody else's pipeline, and what said so
    // was a validation message about a descriptor set nothing here had
    // asked for.
    renderer->skyboxProgram = FluxionShaderProgramHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->skyboxVertexBuffer = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->skyboxPipeline = FluxionRHIPipelineHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->skyboxFrameLayout = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };

    // Written out rather than left to the zeroing above: a zeroed handle
    // has index zero, and index zero is a real slot -- so an unbuilt
    // program would read as one that was already there.
    renderer->irradianceProgram = FluxionShaderProgramHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->irradiancePipeline = FluxionRHIPipelineHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->irradianceLayout = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->irradianceBindGroup = FluxionRHIBindGroupHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };

    // Invalid, not zero: a zeroed handle is a handle to slot zero, and the
    // last time this engine left one that way the first frame destroyed
    // somebody else's texture.
    renderer->sceneColorTexture = FluxionRHITextureHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->sceneColorView = FluxionRHITextureViewHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->sceneColorSampleView = FluxionRHITextureViewHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->sceneColorIsUndefined = true;
    renderer->postProgram = FluxionShaderProgramHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->postPipeline = FluxionRHIPipelineHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->postLayout = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->postSampler = FluxionRHISamplerHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->postVertexBuffer = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->postUniformBuffer = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->postBindGroup = FluxionRHIBindGroupHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->postBoundSceneColor = FluxionRHITextureViewHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->postOutputFormat = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    renderer->irradianceFailed = false;

    renderer->prefilterProgram = FluxionShaderProgramHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->prefilterPipeline = FluxionRHIPipelineHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->prefilterLayout = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    for (u32 mip = 0; mip < FLUXION_RENDERER_PREFILTERED_MIPS; ++mip)
    {
        renderer->prefilterBindGroups[mip] = FluxionRHIBindGroupHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }
    renderer->dfgProgram = FluxionShaderProgramHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->dfgPipeline = FluxionRHIPipelineHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->dfgLayout = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->dfgBindGroup = FluxionRHIBindGroupHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->environmentParamsBuffer = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->environmentScratchBuffer = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->prefilterFailed = false;

    renderer->shadowProgram = FluxionShaderProgramHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->shadowPipeline = FluxionRHIPipelineHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->shadowGlobalLayout = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    for (u32 i = 0; i < FLUXION_RENDER_VIEW_MAX_SHADOWS; ++i)
    {
        renderer->shadowGlobalBindGroups[i] = FluxionRHIBindGroupHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }
    renderer->shadowMatrixBuffer = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->shadowPipelineBuilt = false;
    renderer->shadowFailed = false;

    // What a caller that never says otherwise gets. It is a guess, and it
    // is why saying so exists: a frame drawn into a colour attachment of
    // any other format needs the pipeline rebuilt against that one.
    renderer->attachmentColorFormat = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    renderer->attachmentDepthFormat = FLUXION_RHI_FORMAT_UNKNOWN;

    FluxionRHIBindGroupLayoutDesc objectLayoutDesc = FluxionRendererInternal_MakeObjectLayoutDesc();
    renderer->objectBindGroupLayout = Fluxion_RHI_CreateBindGroupLayout(device, &objectLayoutDesc);

    // The frame's object list, made with the renderer rather than on
    // first use: every pass below reads it, and a pass that had to check
    // whether it existed yet would be checking on every draw of every
    // frame for a thing that is either there from the start or nowhere.
    renderer->gpuScene = Fluxion_GPUScene_Create(device);
    if (!FLUXION_HANDLE_IS_VALID(renderer->gpuScene))
    {
        FLUXION_LOG_ERROR("Renderer", "The frame's object list could not be made; nothing would be drawn.");
    }

    CreateDebugDrawResources(*renderer);

    FluxionRenderGraphPassType passType;
    passType.name = "ForwardOpaquePass";
    passType.setup = FluxionForwardOpaquePass_Setup;
    passType.execute = FluxionForwardOpaquePass_Execute;
    if (!Fluxion_RenderGraphPassRegistry_Register(&passType))
    {
        FLUXION_LOG_ERROR("Renderer", "Failed to register \"ForwardOpaquePass\" (already registered? Only one FluxionRenderer instance is supported at a time)");
    }

    FluxionRenderGraphPassType pyramidPassType;
    pyramidPassType.name = "DepthPyramidPass";
    pyramidPassType.setup = FluxionDepthPyramidPass_Setup;
    pyramidPassType.execute = FluxionDepthPyramidPass_Execute;
    if (!Fluxion_RenderGraphPassRegistry_Register(&pyramidPassType))
    {
        FLUXION_LOG_ERROR("Renderer", "Failed to register \"DepthPyramidPass\" (already registered? Only one FluxionRenderer instance is supported at a time)");
    }

    FluxionRenderGraphPassType motionPassType;
    motionPassType.name = "MotionVectorPass";
    motionPassType.setup = FluxionMotionVectorPass_Setup;
    motionPassType.execute = FluxionMotionVectorPass_Execute;
    if (!Fluxion_RenderGraphPassRegistry_Register(&motionPassType))
    {
        FLUXION_LOG_ERROR("Renderer", "Failed to register \"MotionVectorPass\" (already registered? Only one FluxionRenderer instance is supported at a time)");
    }

    FluxionRenderGraphPassType postPassType;
    postPassType.name = "PostProcessPass";
    postPassType.setup = FluxionPostProcessPass_Setup;
    postPassType.execute = FluxionPostProcessPass_Execute;
    if (!Fluxion_RenderGraphPassRegistry_Register(&postPassType))
    {
        FLUXION_LOG_ERROR("Renderer", "Failed to register \"PostProcessPass\" (already registered? Only one FluxionRenderer instance is supported at a time)");
    }

    FluxionRenderGraphPassType shadowPassType;
    shadowPassType.name = "ShadowPass";
    shadowPassType.setup = FluxionShadowPass_Setup;
    shadowPassType.execute = FluxionShadowPass_Execute;
    if (!Fluxion_RenderGraphPassRegistry_Register(&shadowPassType))
    {
        FLUXION_LOG_ERROR("Renderer", "Failed to register \"ShadowPass\" (already registered? Only one FluxionRenderer instance is supported at a time)");
    }

    FluxionRendererHandle handle = { index, generation };
    return handle;
}

extern "C" void Fluxion_Renderer_Destroy(FluxionRendererHandle rendererHandle)
{
    FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr)
    {
        FLUXION_ASSERT_MSG(false, "Fluxion_Renderer_Destroy called with an invalid or already-destroyed handle");
        return;
    }

    // The sky goes with the renderer that built it.
    FluxionRendererInternal_PostProcess_Shutdown(renderer);
    FluxionRendererInternal_Skybox_Destroy(renderer);
    FluxionRendererInternal_Irradiance_Destroy(renderer);
    FluxionRendererInternal_Prefilter_Destroy(renderer);
    FluxionRendererInternal_Shadow_Destroy(renderer);
    FluxionRendererInternal_MotionVector_Destroy(renderer);
    FluxionRendererInternal_DepthPyramid_Destroy(renderer);
    FluxionRendererInternal_History_Destroy(renderer);

    Fluxion_RenderGraphPassRegistry_Unregister("ForwardOpaquePass");
    Fluxion_RenderGraphPassRegistry_Unregister("ShadowPass");
    Fluxion_RenderGraphPassRegistry_Unregister("MotionVectorPass");
    Fluxion_RenderGraphPassRegistry_Unregister("DepthPyramidPass");

    if (FLUXION_HANDLE_IS_VALID(renderer->gpuScene)) Fluxion_GPUScene_Destroy(renderer->gpuScene);
    if (FLUXION_HANDLE_IS_VALID(renderer->objectBindGroupLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->objectBindGroupLayout);
    if (FLUXION_HANDLE_IS_VALID(renderer->debugVertexBuffer))
    {
        Fluxion_RHI_DestroyBuffer(renderer->debugVertexBuffer);
        // The alloc was recorded at create; forgetting this line is
        // precisely what the tracker's shutdown warning exists to catch
        // -- and did.
        FluxionRendererInternal_RecordGpuFree(false, sizeof(renderer->debugVertices[0]) * FLUXION_RENDERER_MAX_DEBUG_VERTICES);
    }
    if (FLUXION_HANDLE_IS_VALID(renderer->debugPipeline)) Fluxion_RHI_DestroyPipeline(renderer->debugPipeline);
    if (FLUXION_HANDLE_IS_VALID(renderer->debugVertexShader)) Fluxion_RHI_DestroyShader(renderer->debugVertexShader);
    if (FLUXION_HANDLE_IS_VALID(renderer->debugFragmentShader)) Fluxion_RHI_DestroyShader(renderer->debugFragmentShader);
    if (FLUXION_HANDLE_IS_VALID(renderer->debugFrameBindGroupLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->debugFrameBindGroupLayout);

    renderer->alive = false;
    ++renderer->generation;
}

extern "C" void Fluxion_Renderer_UpdateEnvironment(FluxionRendererHandle rendererHandle, FluxionRenderViewHandle view,
                                                   FluxionRHICommandListHandle commandList)
{
    FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr) return;

    // The table first: it shares the scratch buffer with the chain, and
    // it only ever runs once per view.
    FluxionRendererInternal_Dfg_Compute(renderer, commandList, view);

    // One flag, taken once, answered by both passes: the coefficients
    // and the prefiltered chain describe the same sky, and refreshing
    // one without the other would light the diffuse and the specular
    // halves of a surface from two different worlds.
    if (!FluxionRendererInternal_RenderView_TakeEnvironmentDirty(view)) return;

    FluxionRendererInternal_Irradiance_Project(renderer, commandList, view);
    FluxionRendererInternal_Prefilter_Project(renderer, commandList, view);
}

extern "C" void Fluxion_Renderer_BeginFrame(FluxionRendererHandle rendererHandle, FluxionRenderViewHandle view)
{
    FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr) return;

    FLUXION_ASSERT_MSG(!renderer->inFrame, "Fluxion_Renderer_BeginFrame called without a matching EndFrame");
    renderer->inFrame = true;
    renderer->currentView = view;
    Fluxion_GPUScene_Begin(renderer->gpuScene);
    renderer->debugVertexCount = 0;
    renderer->lastDrawCallCount = 0;

    // --- what the frame before this one left behind ---------------------
    //
    // The history is the renderer's, and this is where it meets the view:
    // the view is made fresh every frame and cannot remember anything, so
    // the matrix it needs to say where a pixel WAS comes from here.
    FluxionViewport viewport{};
    if (FluxionRendererInternal_RenderView_GetViewport(view, &viewport) && viewport.width > 0.0f && viewport.height > 0.0f)
    {
        const u32 width = (u32)viewport.width;
        const u32 height = (u32)viewport.height;

        // The scene's own target follows the frame's size, like the
        // history does -- and is made before anything draws into it.
        if (renderer->postEnabled)
        {
            FluxionRendererInternal_PostProcess_EnsureSceneColor(renderer, width, height);
        }

        const bool sameSize = renderer->history.width == width && renderer->history.height == height;
        if (FluxionRendererInternal_History_Begin(renderer, width, height))
        {
            // Readable only if there was a frame before this one AND it
            // was the same shape. Everything else -- a camera that
            // teleported -- is the caller's to say, because no threshold
            // in metres tells a jump from a fast pan.
            renderer->history.valid = renderer->history.hasPreviousViewProjection && sameSize;
        }
    }

    if (renderer->history.hasPreviousViewProjection)
    {
        Fluxion_RenderView_SetPreviousViewProjection(view, renderer->history.previousViewProjection);
    }

    // Told once a frame rather than assumed: with the chain on, the
    // surface shaders write light and the resolve turns it into a
    // picture; with it off, each shader does its own as it always did.
    FluxionRendererInternal_RenderView_SetToneMappingDeferred(view, renderer->postEnabled && FLUXION_HANDLE_IS_VALID(renderer->sceneColorTexture));

    // AND WRITTEN AGAIN, because the answer above changes one of them.
    // A caller that fills its frame constants before beginning the frame
    // -- which is the natural order, everything about the frame is known
    // by then -- would otherwise send constants that still say every
    // shader does its own camera and curve, and the resolve would then
    // do it a second time. Measured, when it did: the picture came out
    // at the exposure squared, which is nearly black.
    Fluxion_RenderView_UpdateFrameConstants(view);
}

extern "C" void Fluxion_Renderer_SetPostProcessEnabled(FluxionRendererHandle rendererHandle, bool enabled)
{
    FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr) return;

    if (renderer->postEnabled == enabled) return;
    renderer->postEnabled = enabled;

    // Off again means the scene goes straight to the caller's target once
    // more, and what was made for the chain has nothing left to hold.
    if (!enabled) FluxionRendererInternal_PostProcess_ReleaseSceneColor(renderer);
}

extern "C" bool Fluxion_Renderer_IsPostProcessEnabled(FluxionRendererHandle rendererHandle)
{
    const FluxionRenderer* renderer = Resolve(rendererHandle);
    return renderer != nullptr && renderer->postEnabled;
}

extern "C" FluxionRHIFormat Fluxion_Renderer_GetSceneColorFormat(void)
{
    return FLUXION_RENDERER_SCENE_COLOR_FORMAT;
}

extern "C" FluxionRHITextureHandle Fluxion_Renderer_GetSceneColorTexture(FluxionRendererHandle rendererHandle)
{
    const FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr) return FluxionRHITextureHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    return renderer->sceneColorTexture;
}

extern "C" void Fluxion_Renderer_SetOutputColorFormat(FluxionRendererHandle rendererHandle, FluxionRHIFormat format)
{
    FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr) return;
    renderer->postOutputFormat = format;
}

extern "C" void Fluxion_Renderer_InvalidateHistory(FluxionRendererHandle rendererHandle)
{
    FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr) return;

    renderer->history.valid = false;
    renderer->history.hasPreviousViewProjection = false;
}

extern "C" bool Fluxion_Renderer_IsHistoryValid(FluxionRendererHandle rendererHandle)
{
    const FluxionRenderer* renderer = Resolve(rendererHandle);
    return renderer != nullptr && renderer->history.valid;
}

extern "C" FluxionRHITextureViewHandle Fluxion_Renderer_GetMotionVectorView(FluxionRendererHandle rendererHandle)
{
    const FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr) return FluxionRHITextureViewHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };

    return renderer->history.motionView;
}

extern "C" void Fluxion_Renderer_DrawMesh(FluxionRendererHandle rendererHandle, FluxionMeshBufferHandle mesh, FluxionMaterialHandle material, FluxionRenderPipelineHandle pipeline, const FluxionMat4* transform)
{
    FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr || !renderer->inFrame) return;

    // Straight into the frame's object list. Nothing is uploaded here
    // any more, and nothing can be: what a shader reads is device memory
    // now, and the ORDER of it is not known until every draw of the
    // frame has been asked for -- see Fluxion_Renderer_UploadScene.
    Fluxion_GPUScene_Add(renderer->gpuScene, mesh, material, pipeline, transform);
}

extern "C" void Fluxion_Renderer_SubmitRenderWorld(FluxionRendererHandle rendererHandle, const FluxionRenderWorld* world)
{
    FLUXION_PROFILE_FUNCTION();

    FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr || !renderer->inFrame || world == nullptr) return;

    for (u32 i = 0; i < world->objectCount; ++i)
    {
        const FluxionRenderObject& object = world->objects[i];

        // The one thing this call does that a loop of DrawMesh would
        // not: what was decided not to be seen is not sent. Nothing
        // decides that yet, so nothing is dropped yet -- and the day
        // something does, every caller already goes through here.
        if (!object.visible) continue;

        Fluxion_GPUScene_AddMoving(renderer->gpuScene, object.mesh, object.material, object.pipeline, &object.transform,
                                   &object.previousTransform, object.layerMask, object.lodIndex);
    }
}

extern "C" void Fluxion_Renderer_UploadScene(FluxionRendererHandle rendererHandle, FluxionRHICommandListHandle commandList)
{
    FLUXION_PROFILE_FUNCTION();

    FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr || !renderer->inFrame) return;

    // What this frame can see, taken from the view it is being drawn
    // through. Handed over here rather than at every DrawMesh: it is one
    // answer for the whole frame, and the objects have all arrived by
    // now.
    FluxionGPUSceneCullDesc cull;
    std::memset(&cull, 0, sizeof(cull));

    FluxionRenderTargetHandle target{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    u32 viewLayerMask = 0;
    FluxionRendererInternal_RenderView_Get(renderer->currentView, &target, &viewLayerMask, nullptr);
    cull.layerMask = viewLayerMask;
    cull.enabled = FluxionRendererInternal_RenderView_GetCamera(renderer->currentView, &cull.viewProjection,
                                                                &cull.cameraPosition, &cull.cullDistance);
    cull.mode = (renderer->cullMode == FLUXION_RENDERER_CULL_ON_DEVICE) ? FLUXION_GPU_SCENE_CULL_GPU : FLUXION_GPU_SCENE_CULL_CPU;

    // AND WHAT THE FRAME BEFORE THIS ONE COULD SEE.
    //
    // The pyramid the history holds is the PREVIOUS frame's -- this
    // frame's is drawn later, by the pass that owns it -- so the camera
    // that goes with it is the previous frame's too. Both come from the
    // same place for that reason.
    cull.occlusionPyramid = renderer->history.pyramidWholeView;
    cull.occlusionSampler = renderer->pyramidSampler;
    cull.previousViewProjection = renderer->history.previousViewProjection;
    cull.pyramidWidth = (f32)renderer->history.width;
    cull.pyramidHeight = (f32)renderer->history.height;
    cull.pyramidLevels = renderer->history.pyramidLevels;

    // Only when there IS a previous frame, and only when what it drew is
    // still the screen this one is drawn on -- the same question the
    // history answers about itself.
    //
    const bool occlusionIsTrustworthy = true;

    cull.occlusionEnabled = renderer->history.valid && renderer->history.pyramidValid && occlusionIsTrustworthy;

    // A PYRAMID NOBODY HAS DRAWN INTO IS STILL BOUND, by the cull pass
    // below -- and a binding says the texture is readable whether or not
    // the shader reads it. So a new one is moved into that state here,
    // once, where there is a command list to say it in.
    if (renderer->history.pyramidNeedsFirstTransition && FLUXION_HANDLE_IS_VALID(renderer->history.pyramidTexture))
    {
        FluxionRHIBarrier toRead{};
        toRead.texture = renderer->history.pyramidTexture;
        toRead.buffer = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
        toRead.before = FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
        toRead.after = FLUXION_RHI_RESOURCE_STATE_SHADER_READ;
        Fluxion_RHI_CommandList_Barrier(commandList, &toRead, 1);

        renderer->history.pyramidNeedsFirstTransition = false;
    }

    Fluxion_GPUScene_SetCulling(renderer->gpuScene, &cull);
    Fluxion_GPUScene_Upload(renderer->gpuScene, commandList, renderer->objectBindGroupLayout);
}

extern "C" void Fluxion_Renderer_EndFrame(FluxionRendererHandle rendererHandle, FluxionRHICommandListHandle commandList)
{
    // The frame's CPU-side tail: object-buffer upload, debug-draw flush,
    // everything between the last draw call and the caller's submit.
    FLUXION_PROFILE_FUNCTION();

    FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr || !renderer->inFrame) return;

    // --- debug draw: recorded directly here, not through the render
    // graph -- see DebugDraw.h's comment on why it stays self-contained.
    //
    // The pipeline is built here, the first time there is anything to
    // draw with it, rather than when the renderer was made: it has to
    // match the colour format of what is being drawn into, and that is
    // something the caller states (Fluxion_Renderer_SetDebugDrawColorFormat)
    // after the renderer already exists.
    if (renderer->debugVertexCount > 0 && !FLUXION_HANDLE_IS_VALID(renderer->debugPipeline)) CreateDebugDrawPipelineObject(*renderer);

    if (renderer->debugVertexCount > 0 && FLUXION_HANDLE_IS_VALID(renderer->debugPipeline))
    {
        void* mapped = Fluxion_RHI_MapBuffer(renderer->debugVertexBuffer);
        if (mapped != nullptr)
        {
            std::memcpy(mapped, renderer->debugVertices, (usize)renderer->debugVertexCount * sizeof(FluxionDebugDrawVertex));
            Fluxion_RHI_UnmapBuffer(renderer->debugVertexBuffer);
        }

        FluxionRHIBindGroupHandle frameBindGroup{ FLUXION_HANDLE_INVALID_INDEX, 0 };
        FluxionRenderTargetHandle renderTarget{ FLUXION_HANDLE_INVALID_INDEX, 0 };
        FluxionRendererInternal_RenderView_Get(renderer->currentView, &renderTarget, nullptr, &frameBindGroup);

        FluxionRHITextureViewHandle colorViews[FLUXION_RENDER_TARGET_MAX_COLOR_ATTACHMENTS];
        u32 colorViewCount = 0;
        FluxionRHITextureViewHandle depthView{ FLUXION_HANDLE_INVALID_INDEX, 0 };
        if (FluxionRendererInternal_RenderTarget_Get(renderTarget, colorViews, &colorViewCount, &depthView) && colorViewCount > 0)
        {
            FluxionViewport viewport{};
            FluxionRendererInternal_RenderView_GetViewport(renderer->currentView, &viewport);

            FluxionRHIRenderingAttachment colorAttachment{};
            colorAttachment.view = colorViews[0];
            colorAttachment.clear = false;

            // The scene's own depth, carried in and not cleared, so what
            // was already drawn can hide a line behind it. Attached only
            // when the caller has said what format it is, because that is
            // the same condition the pipeline was built under -- attaching
            // one the pipeline was not built against is a mismatch, not a
            // bonus.
            FluxionRHIRenderingAttachment depthAttachment{};
            const bool testAgainstDepth =
                renderer->attachmentDepthFormat != FLUXION_RHI_FORMAT_UNKNOWN && FLUXION_HANDLE_IS_VALID(depthView);
            if (testAgainstDepth)
            {
                depthAttachment.view = depthView;
                depthAttachment.clear = false;
            }

            FluxionRHIRenderingDesc renderingDesc{};
            renderingDesc.colorAttachments = &colorAttachment;
            renderingDesc.colorAttachmentCount = 1;
            renderingDesc.depthAttachment = testAgainstDepth ? &depthAttachment : nullptr;
            renderingDesc.width = (u32)viewport.width;
            renderingDesc.height = (u32)viewport.height;

            Fluxion_RHI_CommandList_BeginRendering(commandList, &renderingDesc);
            Fluxion_RHI_CommandList_SetPipeline(commandList, renderer->debugPipeline);
            Fluxion_RHI_CommandList_SetVertexBuffer(commandList, 0, renderer->debugVertexBuffer, 0);
            if (FLUXION_HANDLE_IS_VALID(frameBindGroup)) Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_FRAME, frameBindGroup);
            Fluxion_RHI_CommandList_Draw(commandList, renderer->debugVertexCount, 1, 0, 0);
            Fluxion_RHI_CommandList_EndRendering(commandList);
        }
    }

    // THIS frame's camera becomes the previous one, and it is taken from
    // the view rather than rebuilt from its parts: whoever reads it next
    // frame has to get the matrix the shaders were given, not one built
    // the same way a second time.
    renderer->history.previousViewProjection = Fluxion_RenderView_GetViewProjection(renderer->currentView);
    renderer->history.hasPreviousViewProjection = true;

    renderer->inFrame = false;
    renderer->debugVertexCount = 0;
}

extern "C" void Fluxion_Renderer_SetDebugDrawColorFormat(FluxionRendererHandle rendererHandle, FluxionRHIFormat format)
{
    FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr || format == renderer->attachmentColorFormat) return;

    FLUXION_ASSERT_MSG(!renderer->inFrame, "Renderer: the debug-draw colour format cannot change in the middle of a frame");

    renderer->attachmentColorFormat = format;

    // Anything already built was built against the format that has just
    // been replaced, so it is thrown away; the next debug draw builds one
    // against the new one. Nothing is built here, because a caller that
    // says this at startup -- which is where it belongs -- may never draw
    // a debug line at all.
    if (FLUXION_HANDLE_IS_VALID(renderer->debugPipeline)) Fluxion_RHI_DestroyPipeline(renderer->debugPipeline);
    renderer->debugPipeline = FluxionRHIPipelineHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
}

extern "C" void Fluxion_Renderer_SetDebugDrawDepthFormat(FluxionRendererHandle rendererHandle, FluxionRHIFormat format)
{
    FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr || format == renderer->attachmentDepthFormat) return;

    FLUXION_ASSERT_MSG(!renderer->inFrame, "Renderer: the debug-draw depth format cannot change in the middle of a frame");

    renderer->attachmentDepthFormat = format;

    // Same reasoning as the colour format above: what was built was built
    // against the old answer, so it goes, and the next debug draw builds
    // one that matches.
    if (FLUXION_HANDLE_IS_VALID(renderer->debugPipeline)) Fluxion_RHI_DestroyPipeline(renderer->debugPipeline);
    renderer->debugPipeline = FluxionRHIPipelineHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
}

extern "C" void* Fluxion_Renderer_GetForwardOpaquePassUserData(FluxionRendererHandle rendererHandle)
{
    return Resolve(rendererHandle);
}

extern "C" FluxionRHITextureHandle Fluxion_Renderer_GetMotionVectorTexture(FluxionRendererHandle rendererHandle)
{
    const FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr) return FluxionRHITextureHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };

    return renderer->history.motionTexture;
}

extern "C" FluxionRHITextureHandle Fluxion_Renderer_GetDepthPyramidTexture(FluxionRendererHandle rendererHandle)
{
    const FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr) return FluxionRHITextureHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };

    return renderer->history.pyramidTexture;
}

extern "C" u32 Fluxion_Renderer_GetDepthPyramidLevelCount(FluxionRendererHandle rendererHandle)
{
    const FluxionRenderer* renderer = Resolve(rendererHandle);
    return renderer != nullptr ? renderer->history.pyramidLevels : 0;
}

extern "C" void Fluxion_Renderer_SetCullMode(FluxionRendererHandle rendererHandle, FluxionRendererCullMode mode)
{
    FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr) return;

    renderer->cullMode = mode;
}

extern "C" FluxionRendererCullMode Fluxion_Renderer_GetCullMode(FluxionRendererHandle rendererHandle)
{
    FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr) return FLUXION_RENDERER_CULL_ON_HOST;

    return renderer->cullMode;
}

extern "C" u32 Fluxion_Renderer_GetVisibleObjectCount(FluxionRendererHandle rendererHandle)
{
    FluxionRenderer* renderer = Resolve(rendererHandle);
    return renderer != nullptr ? Fluxion_GPUScene_GetVisibleCount(renderer->gpuScene) : 0;
}

extern "C" u32 Fluxion_Renderer_GetLastDrawCallCount(FluxionRendererHandle rendererHandle)
{
    FluxionRenderer* renderer = Resolve(rendererHandle);
    return renderer != nullptr ? renderer->lastDrawCallCount : 0;
}
