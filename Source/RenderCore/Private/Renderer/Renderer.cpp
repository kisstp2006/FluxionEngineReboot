// C++ only for the debug-draw pipeline's shader compile step (dxc's
// HLSL-text -> SPIR-V-bytes adapter is a Fluxion::ShaderCompiler C++
// API) -- everything else here is plain struct bookkeeping that would
// have been just as happy in C.

#include <Fluxion/RenderCore/Renderer/Renderer.h>

#include "RendererInternal.h"

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
    const bool testAgainstDepth = renderer.debugDepthFormat != FLUXION_RHI_FORMAT_UNKNOWN;
    pipelineDesc.depthState.testEnable = testAgainstDepth;
    pipelineDesc.depthState.compareOp = FLUXION_RHI_COMPARE_OP_LESS_OR_EQUAL;
    pipelineDesc.depthState.writeEnable = false;
    pipelineDesc.blendState.blendEnable = true;
    pipelineDesc.topology = FLUXION_RHI_PRIMITIVE_TOPOLOGY_LINE_LIST; // triangles are appended as their own 3 vertices too -- see DebugDraw.c
    pipelineDesc.colorFormats[0] = renderer.debugColorFormat;
    pipelineDesc.colorFormatCount = 1;
    pipelineDesc.depthFormat = renderer.debugDepthFormat;
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
    // too, not just objectBuffer/currentView, or a later Destroy call
    // would try to free a bogus "valid" handle instead of skipping it.
    renderer->currentView = FluxionRenderViewHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->objectBuffer = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->debugVertexBuffer = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->debugVertexShader = FluxionRHIShaderHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->debugFragmentShader = FluxionRHIShaderHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->debugPipeline = FluxionRHIPipelineHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->debugFrameBindGroupLayout = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };

    // What a caller that never says otherwise gets. It is a guess, and it
    // is why saying so exists: a frame drawn into a colour attachment of
    // any other format needs the pipeline rebuilt against that one.
    renderer->debugColorFormat = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    renderer->debugDepthFormat = FLUXION_RHI_FORMAT_UNKNOWN;

    FluxionRHIBindGroupLayoutDesc objectLayoutDesc = FluxionRendererInternal_MakeObjectLayoutDesc();
    renderer->objectBindGroupLayout = Fluxion_RHI_CreateBindGroupLayout(device, &objectLayoutDesc);

    CreateDebugDrawResources(*renderer);

    FluxionRenderGraphPassType passType;
    passType.name = "ForwardOpaquePass";
    passType.setup = FluxionForwardOpaquePass_Setup;
    passType.execute = FluxionForwardOpaquePass_Execute;
    if (!Fluxion_RenderGraphPassRegistry_Register(&passType))
    {
        FLUXION_LOG_ERROR("Renderer", "Failed to register \"ForwardOpaquePass\" (already registered? Only one FluxionRenderer instance is supported at a time)");
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

    Fluxion_RenderGraphPassRegistry_Unregister("ForwardOpaquePass");

    if (FLUXION_HANDLE_IS_VALID(renderer->objectBuffer)) Fluxion_RHI_DestroyBuffer(renderer->objectBuffer);
    if (FLUXION_HANDLE_IS_VALID(renderer->objectBindGroupLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->objectBindGroupLayout);
    if (FLUXION_HANDLE_IS_VALID(renderer->debugVertexBuffer)) Fluxion_RHI_DestroyBuffer(renderer->debugVertexBuffer);
    if (FLUXION_HANDLE_IS_VALID(renderer->debugPipeline)) Fluxion_RHI_DestroyPipeline(renderer->debugPipeline);
    if (FLUXION_HANDLE_IS_VALID(renderer->debugVertexShader)) Fluxion_RHI_DestroyShader(renderer->debugVertexShader);
    if (FLUXION_HANDLE_IS_VALID(renderer->debugFragmentShader)) Fluxion_RHI_DestroyShader(renderer->debugFragmentShader);
    if (FLUXION_HANDLE_IS_VALID(renderer->debugFrameBindGroupLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->debugFrameBindGroupLayout);

    renderer->alive = false;
    ++renderer->generation;
}

extern "C" void Fluxion_Renderer_BeginFrame(FluxionRendererHandle rendererHandle, FluxionRenderViewHandle view)
{
    FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr) return;

    FLUXION_ASSERT_MSG(!renderer->inFrame, "Fluxion_Renderer_BeginFrame called without a matching EndFrame");
    renderer->inFrame = true;
    renderer->currentView = view;
    renderer->packetCount = 0;
    renderer->debugVertexCount = 0;
    renderer->lastDrawCallCount = 0;
}

extern "C" void Fluxion_Renderer_DrawMesh(FluxionRendererHandle rendererHandle, FluxionMeshBufferHandle mesh, FluxionMaterialHandle material, FluxionRenderPipelineHandle pipeline, const FluxionMat4* transform)
{
    FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr || !renderer->inFrame) return;

    if (renderer->packetCount >= FLUXION_RENDERER_MAX_DRAW_PACKETS_PER_FRAME)
    {
        FLUXION_ASSERT_MSG(false, "Renderer: exceeded FLUXION_RENDERER_MAX_DRAW_PACKETS_PER_FRAME draw calls in one frame");
        return;
    }

    u32 slot = renderer->packetCount++;
    renderer->packetTransforms[slot] = (transform != nullptr) ? *transform : Fluxion_Mat4_Identity();

    FluxionDrawPacket& packet = renderer->packets[slot];
    packet.mesh = mesh;
    packet.material = material;
    packet.pipeline = pipeline;
    // OBJECT is bound as a uniform (constant) buffer, not a storage
    // buffer -- it's genuinely just "one small read-only struct per
    // draw", never written by a shader, and D3D12 constant-buffer-view
    // offsets/sizes must land on 256-byte boundaries, so each draw's
    // slice is padded out to that stride even though FluxionMat4 itself
    // is only 64 bytes.
    packet.objectDataOffset = slot * FLUXION_RENDERER_OBJECT_BUFFER_STRIDE;
    packet.layerMask = 0xFFFFFFFFu;

    // --- grow (by doubling) + upload the OBJECT storage buffer, right
    // now, not deferred to EndFrame ----------------------------------------
    //
    // A caller's typical frame is BeginFrame -> DrawMesh (any number of
    // times) -> [compile + execute a render graph containing
    // "ForwardOpaquePass", using the same command list] -> EndFrame; that
    // Execute call reads this buffer, and it happens strictly between
    // DrawMesh and EndFrame, so the buffer must already be sized and
    // populated by the time this call returns, not whenever EndFrame
    // eventually runs. The buffer is CPU_TO_GPU (host-visible), so
    // "uploading" is a plain Map/memcpy/Unmap -- no command-list copy or
    // barrier is needed for this, unlike a GPU_ONLY resource.
    if (renderer->packetCount > renderer->objectBufferCapacity)
    {
        u32 newCapacity = (renderer->objectBufferCapacity == 0) ? 64u : renderer->objectBufferCapacity;
        while (newCapacity < renderer->packetCount) newCapacity *= 2;

        if (FLUXION_HANDLE_IS_VALID(renderer->objectBuffer)) Fluxion_RHI_DestroyBuffer(renderer->objectBuffer);

        FluxionRHIBufferDesc bufferDesc;
        bufferDesc.size = (usize)newCapacity * FLUXION_RENDERER_OBJECT_BUFFER_STRIDE;
        bufferDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER;
        bufferDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
        bufferDesc.debugName = "Fluxion.Renderer.ObjectBuffer";
        renderer->objectBuffer = Fluxion_RHI_CreateBuffer(renderer->device, &bufferDesc);
        renderer->objectBufferCapacity = newCapacity;
    }

    if (FLUXION_HANDLE_IS_VALID(renderer->objectBuffer))
    {
        void* mapped = Fluxion_RHI_MapBuffer(renderer->objectBuffer);
        if (mapped != nullptr)
        {
            std::memcpy((u8*)mapped + packet.objectDataOffset, &renderer->packetTransforms[slot], sizeof(FluxionMat4));
            Fluxion_RHI_UnmapBuffer(renderer->objectBuffer);
        }
    }
}

extern "C" void Fluxion_Renderer_EndFrame(FluxionRendererHandle rendererHandle, FluxionRHICommandListHandle commandList)
{
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
                renderer->debugDepthFormat != FLUXION_RHI_FORMAT_UNKNOWN && FLUXION_HANDLE_IS_VALID(depthView);
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

    renderer->inFrame = false;
    renderer->packetCount = 0;
    renderer->debugVertexCount = 0;
}

extern "C" void Fluxion_Renderer_SetDebugDrawColorFormat(FluxionRendererHandle rendererHandle, FluxionRHIFormat format)
{
    FluxionRenderer* renderer = Resolve(rendererHandle);
    if (renderer == nullptr || format == renderer->debugColorFormat) return;

    FLUXION_ASSERT_MSG(!renderer->inFrame, "Renderer: the debug-draw colour format cannot change in the middle of a frame");

    renderer->debugColorFormat = format;

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
    if (renderer == nullptr || format == renderer->debugDepthFormat) return;

    FLUXION_ASSERT_MSG(!renderer->inFrame, "Renderer: the debug-draw depth format cannot change in the middle of a frame");

    renderer->debugDepthFormat = format;

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

extern "C" u32 Fluxion_Renderer_GetLastDrawCallCount(FluxionRendererHandle rendererHandle)
{
    FluxionRenderer* renderer = Resolve(rendererHandle);
    return renderer != nullptr ? renderer->lastDrawCallCount : 0;
}
