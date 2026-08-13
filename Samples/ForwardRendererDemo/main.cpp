// Manual test tool, not an automated test (same role as Samples/InputDemo):
// opens a window, brings up the Vulkan/OpenGL/D3D12 RHI backend, and every
// frame draws a rotating, textured, depth-tested 3D cube through the
// RenderCore layer (ShaderProgram/Material/MeshBuffer/RenderPipeline/
// RenderView/Renderer + a "ForwardOpaquePass" render graph node) rather
// than hand-rolled RHI calls -- a visible, on-screen demonstration that the
// whole layer actually works end to end on real hardware, across all
// three real backends, that RHITests' offscreen checks can't show on
// their own. The cube's material tint is animated every frame via
// Fluxion_Material_SetVec3 + Fluxion_Material_FlushDirty, proving the
// runtime-settable-material-parameter path specifically. Vertex/index/
// texture data still go CPU->staging buffer->GPU_ONLY resource via
// Map/Unmap + CommandList Copy + a Barrier by hand (MeshBuffer only owns
// vertex/index buffers, not arbitrary textures, and RenderTarget/RenderView
// only wrap already-created views, they don't create textures) -- the same
// staging pattern real game code would use. Shader source is written in
// the engine's own shading language (Shaders/cube.vert.jsl,
// Shaders/cube.frag.jsl) and compiled by Fluxion_ShaderProgram_Create
// itself; this file never touches backend-specific bytecode directly.
#include <Fluxion/Application/Events/EventQueue.h>
#include <Fluxion/Application/Window/Window.h>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/Math.h>
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
#include <Fluxion/Scene/Scene.h>
#include <Fluxion/Scene/SceneScript.hpp>
#include <Fluxion/Script/Script.hpp>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

typedef struct FluxionDemoVertex
{
    f32 position[3];
    f32 uv[2];
} FluxionDemoVertex;

#define FLUXION_DEMO_FRAMES_IN_FLIGHT 2

namespace
{

std::string ReadFile(const char* path)
{
    std::ifstream file(path);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

void ReportScriptDiagnostics(const Fluxion::Script::DiagnosticList& diagnostics)
{
    for (const Fluxion::Script::Diagnostic& entry : diagnostics.entries)
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "%s:%u:%u: %s", entry.location.file.c_str(), entry.location.line,
            entry.location.column, entry.message.c_str());
    }
}

// --- Small local math helpers ------------------------------------------
//
// Foundation/Math.h explicitly keeps view/projection/rotation helpers out
// of FluxionMat4's own module ("those belong with a future renderer/RHI
// layer, not Foundation") -- these live here instead, as this demo's own
// renderer-shaped code. Convention: FluxionMat4::m[row][col] holds the
// standard mathematical entry M_ij (row-major *storage*, ordinary
// matrix-multiply semantics via Fluxion_Mat4_Multiply), and a shader-side
// `viewProjection * model[0] * Vector4(position, 1.0)` treats the vector
// as a column vector (v' = M v). Both GLSL's default uniform-block matrix
// layout and HLSL's default cbuffer/StructuredBuffer layout are
// column-major, so TransposeForUpload converts a row-major-authored
// matrix into that column-major byte layout right before it's handed to
// RenderCore -- both Fluxion_RenderView_UpdateFrameConstants (the FRAME
// viewProjection) and Fluxion_Renderer_DrawMesh (the OBJECT model matrix)
// upload their inputs with a plain memcpy, no transpose step of their
// own, so the caller must supply already-transposed matrices or the GPU
// reconstructs the transpose of the matrix actually intended.

FluxionMat4 MakePerspective(f32 fovYRadians, f32 aspect, f32 nearZ, f32 farZ)
{
    FluxionMat4 m;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            m.m[r][c] = 0.0f;
    f32 f = 1.0f / std::tan(fovYRadians * 0.5f);
    m.m[0][0] = f / aspect;
    m.m[1][1] = f;
    m.m[2][2] = (farZ + nearZ) / (nearZ - farZ);
    m.m[2][3] = (2.0f * farZ * nearZ) / (nearZ - farZ);
    m.m[3][2] = -1.0f;
    return m;
}

FluxionMat4 TransposeForUpload(FluxionMat4 m)
{
    FluxionMat4 r;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            r.m[row][col] = m.m[col][row];
    return r;
}

} // namespace

int main(int argc, char** argv)
{
    // --graphics=vulkan (default) | --graphics=opengl | --graphics=d3d12 --
    // selects which RHI backend this demo drives. Every RenderCore call
    // below is identical regardless of backend -- ShaderProgram.cpp's own
    // CompileStage is what branches per backend now (GLSL text for
    // OpenGL, DXIL for D3D12, SPIR-V for Vulkan/Null), not this file.
    FluxionRHIBackendType backendType = FLUXION_RHI_BACKEND_VULKAN;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--graphics=opengl") == 0) backendType = FLUXION_RHI_BACKEND_OPENGL;
        else if (std::strcmp(argv[i], "--graphics=vulkan") == 0) backendType = FLUXION_RHI_BACKEND_VULKAN;
        else if (std::strcmp(argv[i], "--graphics=d3d12") == 0) backendType = FLUXION_RHI_BACKEND_D3D12;
    }
    const char* backendName = backendType == FLUXION_RHI_BACKEND_OPENGL ? "OpenGL" : backendType == FLUXION_RHI_BACKEND_D3D12 ? "D3D12" : "Vulkan";
    FluxionEventQueue queue;
    Fluxion_EventQueue_Init(&queue, NULL, 256);
    Fluxion_WindowSystem_Init(NULL, &queue, 1);

    // The backend is otherwise only visible in the startup log line below
    // ("Using adapter: ...") -- putting it in the title too means it's
    // visible at a glance even if the demo wasn't launched from a console.
    std::string windowTitle = std::string("Fluxion ForwardRendererDemo [") + backendName + "] (close the window to quit)";
    FluxionWindowDesc windowDesc;
    windowDesc.title = windowTitle.c_str();
    windowDesc.width = 800;
    windowDesc.height = 600;
    windowDesc.resizable = true;
    FluxionWindowHandle window = Fluxion_Window_Create(&windowDesc);

    FluxionRHIInstanceDesc instanceDesc = { "ForwardRendererDemo", true };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(backendType, &instanceDesc);
    if (!FLUXION_HANDLE_IS_VALID(instance))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to create a %s instance -- no usable %s loader/driver on this machine.", backendName, backendName);
        return 1;
    }

    FluxionRHIAdapterHandle adapter;
    if (Fluxion_RHI_EnumerateAdapters(instance, &adapter, 1) == 0)
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "No %s adapter found.", backendName);
        return 1;
    }
    FluxionRHIAdapterInfo adapterInfo;
    Fluxion_RHI_GetAdapterInfo(adapter, &adapterInfo);
    FLUXION_LOG_INFO("ForwardRendererDemo", "Using adapter: %s", adapterInfo.name);

    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapter, &deviceDesc);
    FluxionRHIQueueHandle graphicsQueue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);

    FluxionRHISwapchainDesc swapchainDesc;
    swapchainDesc.width = windowDesc.width;
    swapchainDesc.height = windowDesc.height;
    swapchainDesc.format = FLUXION_RHI_FORMAT_B8G8R8A8_UNORM;
    swapchainDesc.bufferCount = FLUXION_DEMO_FRAMES_IN_FLIGHT;
    swapchainDesc.vsync = true;
    FluxionRHISwapchainHandle swapchain = Fluxion_RHI_CreateSwapchain(device, window, &swapchainDesc);

    // --- Cube geometry: staging (CPU_TO_GPU) -> GPU_ONLY, the real upload
    // pattern, not just a directly-mapped GPU buffer. 24 vertices (4 per
    // face x 6 faces) rather than a shared 8-vertex cube, so each face
    // gets its own correct UVs. -------------------------------------------

    static const FluxionDemoVertex vertices[24] =
    {
        // +Z (front)
        { { -0.5f, -0.5f,  0.5f }, { 0.0f, 1.0f } },
        { {  0.5f, -0.5f,  0.5f }, { 1.0f, 1.0f } },
        { {  0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f } },
        { { -0.5f,  0.5f,  0.5f }, { 0.0f, 0.0f } },
        // -Z (back)
        { {  0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f } },
        { { -0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f } },
        { { -0.5f,  0.5f, -0.5f }, { 1.0f, 0.0f } },
        { {  0.5f,  0.5f, -0.5f }, { 0.0f, 0.0f } },
        // -X (left)
        { { -0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f } },
        { { -0.5f, -0.5f,  0.5f }, { 1.0f, 1.0f } },
        { { -0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f } },
        { { -0.5f,  0.5f, -0.5f }, { 0.0f, 0.0f } },
        // +X (right)
        { {  0.5f, -0.5f,  0.5f }, { 0.0f, 1.0f } },
        { {  0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f } },
        { {  0.5f,  0.5f, -0.5f }, { 1.0f, 0.0f } },
        { {  0.5f,  0.5f,  0.5f }, { 0.0f, 0.0f } },
        // +Y (top)
        { { -0.5f,  0.5f,  0.5f }, { 0.0f, 1.0f } },
        { {  0.5f,  0.5f,  0.5f }, { 1.0f, 1.0f } },
        { {  0.5f,  0.5f, -0.5f }, { 1.0f, 0.0f } },
        { { -0.5f,  0.5f, -0.5f }, { 0.0f, 0.0f } },
        // -Y (bottom)
        { { -0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f } },
        { {  0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f } },
        { {  0.5f, -0.5f,  0.5f }, { 1.0f, 0.0f } },
        { { -0.5f, -0.5f,  0.5f }, { 0.0f, 0.0f } },
    };
    static const u16 indices[36] =
    {
        0, 1, 2, 0, 2, 3,       // front
        4, 5, 6, 4, 6, 7,       // back
        8, 9, 10, 8, 10, 11,    // left
        12, 13, 14, 12, 14, 15, // right
        16, 17, 18, 16, 18, 19, // top
        20, 21, 22, 20, 22, 23, // bottom
    };

    // --- Checkerboard texture: staged CPU->GPU by hand (MeshBuffer only
    // owns vertex/index buffers, not arbitrary textures), then copied
    // into a sampled texture via Fluxion_RHI_CommandList_CopyBufferToTexture. --

    const u32 kTextureSize = 64;
    std::vector<u8> checkerPixels(kTextureSize * kTextureSize * 4);
    for (u32 y = 0; y < kTextureSize; ++y)
    {
        for (u32 x = 0; x < kTextureSize; ++x)
        {
            bool light = (((x / 8) + (y / 8)) % 2) == 0;
            u8 v = light ? 230 : 40;
            u8* px = &checkerPixels[(y * kTextureSize + x) * 4];
            px[0] = v; px[1] = v; px[2] = v; px[3] = 255;
        }
    }

    FluxionRHIBufferDesc textureStagingDesc = { checkerPixels.size(), FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC, FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU, "DemoTextureStaging" };
    FluxionRHIBufferHandle textureStagingBuffer = Fluxion_RHI_CreateBuffer(device, &textureStagingDesc);
    void* mappedTexture = Fluxion_RHI_MapBuffer(textureStagingBuffer);
    memcpy(mappedTexture, checkerPixels.data(), checkerPixels.size());
    Fluxion_RHI_UnmapBuffer(textureStagingBuffer);

    FluxionRHITextureDesc albedoTextureDesc;
    memset(&albedoTextureDesc, 0, sizeof(albedoTextureDesc));
    albedoTextureDesc.width = kTextureSize;
    albedoTextureDesc.height = kTextureSize;
    albedoTextureDesc.depth = 1;
    albedoTextureDesc.mipLevels = 1;
    albedoTextureDesc.arrayLayers = 1;
    albedoTextureDesc.sampleCount = 1;
    albedoTextureDesc.format = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    albedoTextureDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_SAMPLED | FLUXION_RHI_TEXTURE_USAGE_TRANSFER_DST;
    albedoTextureDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    albedoTextureDesc.debugName = "DemoAlbedoTexture";
    FluxionRHITextureHandle albedoTexture = Fluxion_RHI_CreateTexture(device, &albedoTextureDesc);

    FluxionRHITextureViewDesc albedoViewDesc = { albedoTexture, albedoTextureDesc.format, 0, 1, 0, 1 };
    FluxionRHITextureViewHandle albedoView = Fluxion_RHI_CreateTextureView(device, &albedoViewDesc);

    FluxionRHISamplerDesc albedoSamplerDesc;
    memset(&albedoSamplerDesc, 0, sizeof(albedoSamplerDesc));
    albedoSamplerDesc.minFilter = FLUXION_RHI_FILTER_LINEAR;
    albedoSamplerDesc.magFilter = FLUXION_RHI_FILTER_LINEAR;
    albedoSamplerDesc.mipFilter = FLUXION_RHI_FILTER_LINEAR;
    albedoSamplerDesc.addressModeU = FLUXION_RHI_ADDRESS_MODE_REPEAT;
    albedoSamplerDesc.addressModeV = FLUXION_RHI_ADDRESS_MODE_REPEAT;
    albedoSamplerDesc.addressModeW = FLUXION_RHI_ADDRESS_MODE_REPEAT;
    albedoSamplerDesc.maxAnisotropy = 1.0f;
    albedoSamplerDesc.debugName = "DemoAlbedoSampler";
    FluxionRHISamplerHandle albedoSampler = Fluxion_RHI_CreateSampler(device, &albedoSamplerDesc);

    // --- Depth buffer: sized once at startup to the initial window
    // extent -- this demo doesn't otherwise handle swapchain-resize edge
    // cases robustly either, so a fixed-size depth target is an
    // acceptable v1 simplification, not a new gap. --------------------------

    FluxionRHITextureDesc depthTextureDesc;
    memset(&depthTextureDesc, 0, sizeof(depthTextureDesc));
    depthTextureDesc.width = windowDesc.width;
    depthTextureDesc.height = windowDesc.height;
    depthTextureDesc.depth = 1;
    depthTextureDesc.mipLevels = 1;
    depthTextureDesc.arrayLayers = 1;
    depthTextureDesc.sampleCount = 1;
    depthTextureDesc.format = FLUXION_RHI_FORMAT_D32_FLOAT;
    depthTextureDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_DEPTH_STENCIL;
    depthTextureDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    depthTextureDesc.debugName = "DemoDepthTexture";
    FluxionRHITextureHandle depthTexture = Fluxion_RHI_CreateTexture(device, &depthTextureDesc);

    FluxionRHITextureViewDesc depthViewDesc = { depthTexture, depthTextureDesc.format, 0, 1, 0, 1 };
    FluxionRHITextureViewHandle depthView = Fluxion_RHI_CreateTextureView(device, &depthViewDesc);

    // --- Upload command list: cube vertex/index buffers via MeshBuffer's
    // own internal staging path, plus this file's own texture copy and the
    // one-time depth/texture layout transitions. -----------------------------

    FluxionMeshBufferDesc cubeMeshDesc{};
    cubeMeshDesc.vertexData = vertices;
    cubeMeshDesc.vertexDataSize = sizeof(vertices);
    cubeMeshDesc.indexData = indices;
    cubeMeshDesc.indexDataSize = sizeof(indices);
    cubeMeshDesc.use16BitIndices = true;
    cubeMeshDesc.vertexLayout.attributes[0].location = 0;
    cubeMeshDesc.vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    cubeMeshDesc.vertexLayout.attributes[0].offset = offsetof(FluxionDemoVertex, position);
    cubeMeshDesc.vertexLayout.attributes[1].location = 1;
    cubeMeshDesc.vertexLayout.attributes[1].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    cubeMeshDesc.vertexLayout.attributes[1].offset = offsetof(FluxionDemoVertex, uv);
    cubeMeshDesc.vertexLayout.attributeCount = 2;
    cubeMeshDesc.vertexLayout.stride = sizeof(FluxionDemoVertex);
    cubeMeshDesc.bounds = FluxionAABB{ FluxionVec3{ -0.5f, -0.5f, -0.5f }, FluxionVec3{ 0.5f, 0.5f, 0.5f } };
    cubeMeshDesc.debugName = "ForwardRendererDemo.Cube";
    FluxionMeshBufferHandle cubeMesh = Fluxion_MeshBuffer_Create(device, graphicsQueue, &cubeMeshDesc);
    if (!FLUXION_HANDLE_IS_VALID(cubeMesh))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to create the cube MeshBuffer.");
        std::exit(1);
    }

    FluxionRHICommandListHandle uploadCommandList = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RHI_CommandList_Begin(uploadCommandList);

    FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionRHIBarrier preCopyBarriers[2];
    preCopyBarriers[0] = FluxionRHIBarrier{ albedoTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_UNDEFINED, FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION };
    preCopyBarriers[1] = FluxionRHIBarrier{ depthTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_UNDEFINED, FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE };
    Fluxion_RHI_CommandList_Barrier(uploadCommandList, preCopyBarriers, 2);

    Fluxion_RHI_CommandList_CopyBufferToTexture(uploadCommandList, textureStagingBuffer, 0, albedoTexture, 0, 0);

    FluxionRHIBarrier postUploadBarrier = { albedoTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION, FLUXION_RHI_RESOURCE_STATE_SHADER_READ };
    Fluxion_RHI_CommandList_Barrier(uploadCommandList, &postUploadBarrier, 1);
    Fluxion_RHI_CommandList_End(uploadCommandList);

    FluxionRHIFenceHandle uploadFence = Fluxion_RHI_CreateFence(device, false);
    Fluxion_RHI_Queue_Submit(graphicsQueue, &uploadCommandList, 1, uploadFence);
    Fluxion_RHI_WaitForFence(uploadFence);
    Fluxion_RHI_DestroyFence(uploadFence);
    Fluxion_RHI_DestroyCommandList(uploadCommandList);
    Fluxion_RHI_DestroyBuffer(textureStagingBuffer);
    Fluxion_RHI_Device_CollectGarbage(device);

    // --- ShaderProgram / Material / RenderPipeline / Renderer, built once
    // at startup through the RenderCore layer rather than hand-rolled RHI
    // bind groups and pipeline descs. -----------------------------------------

    Fluxion_RenderGraphPassRegistry_Init(); // must run before Fluxion_Renderer_Create, which registers "ForwardOpaquePass" into it

    std::string cubeVertexSource = ReadFile(FLUXION_DEMO_SHADER_DIR "/cube.vert.jsl");
    std::string cubeFragmentSource = ReadFile(FLUXION_DEMO_SHADER_DIR "/cube.frag.jsl");
    if (cubeVertexSource.empty() || cubeFragmentSource.empty())
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to read cube.vert.jsl/cube.frag.jsl from %s", FLUXION_DEMO_SHADER_DIR);
        std::exit(1);
    }

    FluxionShaderProgramDesc cubeProgramDesc{};
    cubeProgramDesc.debugName = "ForwardRendererDemo.CubeProgram";
    cubeProgramDesc.vertexSource = cubeVertexSource.c_str();
    cubeProgramDesc.fragmentSource = cubeFragmentSource.c_str();
    FluxionShaderProgramHandle cubeProgram = Fluxion_ShaderProgram_Create(device, &cubeProgramDesc);
    if (!FLUXION_HANDLE_IS_VALID(cubeProgram))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to create the cube ShaderProgram (see prior dxc/compile errors above).");
        std::exit(1);
    }

    FluxionMaterialHandle cubeMaterial = Fluxion_Material_Create(device, cubeProgram);
    if (!FLUXION_HANDLE_IS_VALID(cubeMaterial))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to create the cube Material.");
        std::exit(1);
    }
    Fluxion_Material_SetTexture(cubeMaterial, "albedoMap", albedoView, albedoSampler);
    Fluxion_Material_SetVec3(cubeMaterial, "tint", FluxionVec3{ 1.0f, 1.0f, 1.0f });
    Fluxion_Material_FlushDirty(cubeMaterial);

    FluxionRenderPipelineHandle cubePipeline = Fluxion_RenderPipeline_Create(device, cubeProgram, FLUXION_RENDER_PIPELINE_CATEGORY_OPAQUE, swapchainDesc.format, depthTextureDesc.format);
    if (!FLUXION_HANDLE_IS_VALID(cubePipeline))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to create the cube RenderPipeline.");
        std::exit(1);
    }

    FluxionRendererHandle renderer = Fluxion_Renderer_Create(device, graphicsQueue);
    if (!FLUXION_HANDLE_IS_VALID(renderer))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to create the FluxionRenderer.");
        std::exit(1);
    }

    // --- Per-frame-in-flight resources (caller-managed, no hidden
    // backend FrameContext) --------------------------------------------------

    FluxionRHICommandListHandle commandLists[FLUXION_DEMO_FRAMES_IN_FLIGHT];
    FluxionRHIFenceHandle frameFences[FLUXION_DEMO_FRAMES_IN_FLIGHT];
    for (u32 i = 0; i < FLUXION_DEMO_FRAMES_IN_FLIGHT; ++i)
    {
        commandLists[i] = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
        frameFences[i] = Fluxion_RHI_CreateFence(device, true);
    }
    // No FluxionRHISemaphoreHandle is created for Acquire/Present: this
    // backend's Acquire already CPU-blocks on an internal fence before
    // returning (VulkanSwapchain.cpp), and Present is preceded by an
    // explicit WaitForFence below -- an unused binary semaphore would
    // just accumulate signals nothing ever consumes (Vulkan requires a
    // semaphore be unsignaled before vkAcquireNextImageKHR signals it
    // again, which a "pass it and never wait on it" semaphore violates
    // on the second frame).
    FluxionRHISemaphoreHandle noSemaphore = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    // --- The cube as a scene object driven by a script component -------
    //
    // Nothing below computes an angle. The demo makes one object, puts a
    // Rotator on it, and hands the scene how much time has passed each
    // frame; what the object then is, is whatever Rotator has made of it.

    FluxionSceneHandle scene = Fluxion_Scene_Create();
    if (!Fluxion_Scene_IsValid(scene))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to create the scene.");
        std::exit(1);
    }

    Fluxion::Script::BindingTable sceneBindings;
    Fluxion::Script::DiagnosticList scriptDiagnostics;
    if (!Fluxion::Scene::BuildBindingTable(scene, sceneBindings, scriptDiagnostics))
    {
        ReportScriptDiagnostics(scriptDiagnostics);
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to describe the scene to the scripting runtime.");
        std::exit(1);
    }

    std::string rotatorSource = ReadFile(FLUXION_DEMO_SCRIPT_DIR "/Rotator.fls");
    if (rotatorSource.empty())
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to read Rotator.fls from %s", FLUXION_DEMO_SCRIPT_DIR);
        std::exit(1);
    }

    Fluxion::Script::CompileOptions scriptOptions;
    scriptOptions.fileName = "Rotator.fls";
    scriptOptions.bindings = &sceneBindings;
    scriptOptions.hostPrelude = Fluxion::Scene::ComponentPreludeSource();

    auto compiledScript = Fluxion::Script::Compile(rotatorSource, scriptOptions, scriptDiagnostics);
    if (!compiledScript.IsOk())
    {
        ReportScriptDiagnostics(scriptDiagnostics);
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to compile Rotator.fls.");
        std::exit(1);
    }

    Fluxion::Script::CompiledModule scriptModule = compiledScript.Value();
    Fluxion::Script::Vm* scriptVm = Fluxion::Script::CreateVm(scriptModule, scriptDiagnostics, &sceneBindings);
    if (!scriptVm || !Fluxion::Scene::AttachRuntime(scene, scriptVm))
    {
        ReportScriptDiagnostics(scriptDiagnostics);
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to start the scripting runtime: %s", Fluxion_Scene_GetLastError(scene));
        std::exit(1);
    }

    FluxionGameObjectHandle cubeObject = Fluxion_Scene_CreateGameObject(scene, "Cube");
    Fluxion_GameObject_SetLocalPosition(scene, cubeObject, FluxionVec3{ 0.0f, 0.0f, -3.0f });

    const u32 rotatorClass = Fluxion::Scene::FindComponentClass(scene, "Rotator");
    if (rotatorClass == Fluxion::Script::kNoClass ||
        Fluxion::Scene::AddComponent(scene, cubeObject, rotatorClass).IsNull())
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to put a Rotator on the cube: %s", Fluxion_Scene_GetLastError(scene));
        std::exit(1);
    }

    FLUXION_LOG_INFO("ForwardRendererDemo", "Window created. Rendering a rotating textured cube every frame through the RenderCore layer.");

    bool running = true;
    u32 frameIndex = 0;
    f32 totalTime = 0.0f;

    // No delta-time tracking in this demo -- a fixed nominal step, which
    // is what the pulse below already assumed, and now also what the
    // scene is told each frame.
    const f32 nominalDeltaTime = 0.016f;
    while (running)
    {
        Fluxion_WindowSystem_PollEvents();
        FluxionEvent event;
        while (Fluxion_EventQueue_Pop(&queue, &event))
        {
            if (event.type == FLUXION_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
        }
        if (!running) break;

        // One turn of the scene: Rotator.Update is what moves the cube.
        Fluxion_Scene_Tick(scene, nominalDeltaTime);
        totalTime += nominalDeltaTime;

        u32 imageIndex = Fluxion_RHI_Swapchain_AcquireNextImage(swapchain, noSemaphore);

        // The swapchain's actual current image extent (queried from the
        // backend) -- NOT a separately-queried window size, which can
        // transiently disagree with the swapchain's own tracked size
        // (window resize race, OS-level border/DPI accounting) and trips
        // a hard Vulkan validation error if the render area doesn't
        // exactly match the acquired image.
        u32 surfaceWidth = 0, surfaceHeight = 0;
        Fluxion_RHI_Swapchain_GetExtent(swapchain, &surfaceWidth, &surfaceHeight);
        FluxionRHITextureHandle backbuffer = Fluxion_RHI_Swapchain_GetTexture(swapchain, imageIndex);

        FluxionRHITextureViewDesc backbufferViewDesc = { backbuffer, swapchainDesc.format, 0, 1, 0, 1 };
        FluxionRHITextureViewHandle backbufferView = Fluxion_RHI_CreateTextureView(device, &backbufferViewDesc);

        FluxionRenderTargetDesc targetDesc{};
        targetDesc.colorViews[0] = backbufferView;
        targetDesc.colorViewCount = 1;
        targetDesc.depthView = depthView;
        FluxionRenderTargetHandle frameTarget = Fluxion_RenderTarget_Create(device, &targetDesc);

        // FRAME viewProjection: no separate camera/view transform in this
        // demo (camera fixed at the origin, same as the previous version
        // of this demo), so viewMatrix is the identity and the
        // perspective projection alone carries the transpose-for-upload
        // step (see TransposeForUpload's comment above -- (AB)^T = B^T A^T,
        // and Fluxion_RenderView_UpdateFrameConstants computes
        // projectionMatrix * viewMatrix with no transpose of its own).
        f32 aspect = surfaceHeight != 0 ? (f32)surfaceWidth / (f32)surfaceHeight : 1.0f;
        FluxionMat4 projection = MakePerspective(1.0472f /* 60 degrees */, aspect, 0.1f, 100.0f);

        FluxionRenderViewDesc viewDesc{};
        viewDesc.viewMatrix = Fluxion_Mat4_Identity();
        viewDesc.projectionMatrix = TransposeForUpload(projection);
        viewDesc.viewport = FluxionViewport{ 0.0f, 0.0f, (f32)surfaceWidth, (f32)surfaceHeight, 0.0f, 1.0f };
        viewDesc.scissor = FluxionScissorRect{ 0, 0, surfaceWidth, surfaceHeight };
        viewDesc.renderTarget = frameTarget;
        viewDesc.layerMask = 0xFFFFFFFFu;
        FluxionRenderViewHandle frameView = Fluxion_RenderView_Create(device, &viewDesc);
        Fluxion_RenderView_UpdateFrameConstants(frameView);

        // Slow color pulse to make the Material.SetVec3 runtime-update
        // path visible on screen -- the whole point of this demo's tint
        // parameter, not a static material.
        f32 pulse = 0.5f + 0.5f * std::sin(totalTime);
        Fluxion_Material_SetVec3(cubeMaterial, "tint", FluxionVec3{ 1.0f, 0.5f + 0.5f * pulse, 0.5f + 0.5f * (1.0f - pulse) });
        Fluxion_Material_FlushDirty(cubeMaterial);

        // OBJECT model matrix: same transpose-for-upload requirement as
        // the FRAME matrix above -- Fluxion_Renderer_DrawMesh's `transform`
        // is memcpy'd verbatim into the OBJECT storage buffer with no
        // transpose of its own (see Renderer.cpp), and cube.vert.jsl reads
        // it as `model[0]` in the same column-major-expecting multiply as
        // viewProjection.
        FluxionMat4 model = Fluxion_GameObject_GetWorldMatrix(scene, cubeObject);
        FluxionMat4 modelForUpload = TransposeForUpload(model);

        FluxionRHICommandListHandle cmd = commandLists[frameIndex];
        Fluxion_RHI_CommandList_Begin(cmd);

        // The backbuffer is always imported as UNDEFINED, not "UNDEFINED
        // on frame 0, PRESENT afterward": a single demo-wide "first frame"
        // flag is wrong the moment there is more than one swapchain image
        // (FLUXION_DEMO_FRAMES_IN_FLIGHT == 2 here) -- image B's own very
        // first use is still genuinely UNDEFINED even on the demo's second
        // iteration of this loop, when only image A has actually been
        // through a real PRESENT transition so far. VK_IMAGE_LAYOUT_UNDEFINED
        // as a barrier's old layout is always valid regardless of the
        // resource's real current layout (it means "discard whatever was
        // there"), which is exactly what's wanted anyway since
        // "ForwardOpaquePass" clears both attachments every frame.
        //
        // The depth texture has none of that ambiguity -- it's one single
        // persistent resource across the whole run, not N cycling
        // swapchain images, so its real current state is always exactly
        // known: DEPTH_WRITE, set once by the upload command list before
        // this loop even starts (see preCopyBarriers above) and left there
        // by every subsequent Execute. D3D12's barrier validation (unlike
        // Vulkan's UNDEFINED-is-always-valid layout) requires the declared
        // "before" state to actually match, so claiming UNDEFINED here
        // every frame is a real mismatch, not just a conservative no-op.
        FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(device);
        Fluxion_RenderGraph_ImportTexture(graph, "ForwardOpaquePass.Color0", backbuffer, FLUXION_RHI_RESOURCE_STATE_UNDEFINED);
        Fluxion_RenderGraph_ImportTexture(graph, "ForwardOpaquePass.Depth", depthTexture, FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE);
        Fluxion_RenderGraph_AddPassFromRegistry(graph, "ForwardOpaquePass", Fluxion_Renderer_GetForwardOpaquePassUserData(renderer));

        Fluxion_Renderer_BeginFrame(renderer, frameView);
        Fluxion_Renderer_DrawMesh(renderer, cubeMesh, cubeMaterial, cubePipeline, &modelForUpload);

        if (!Fluxion_RenderGraph_Compile(graph))
        {
            FLUXION_LOG_ERROR("ForwardRendererDemo", "Render graph compilation failed -- this is a real bug, not a transient condition.");
            std::exit(1);
        }
        Fluxion_RenderGraph_Execute(graph, cmd);
        Fluxion_Renderer_EndFrame(renderer, cmd);

        // The render graph's own compiled barrier list ends the backbuffer
        // in whatever state "ForwardOpaquePass" last wrote it as
        // (RENDER_TARGET) -- RenderGraphCompiler.cpp never transitions an
        // imported resource back to its import-time state at the end of
        // Execute, so Present still needs one manual barrier here, the
        // same role the old hand-rolled demo's own toPresent barrier
        // played.
        FluxionRHIBarrier toPresent = { backbuffer, noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET, FLUXION_RHI_RESOURCE_STATE_PRESENT };
        Fluxion_RHI_CommandList_Barrier(cmd, &toPresent, 1);

        Fluxion_RHI_CommandList_End(cmd);

        Fluxion_RHI_Queue_Submit(graphicsQueue, &cmd, 1, frameFences[frameIndex]);

        // WaitForFence can time out (backend-defined bound, not infinite)
        // instead of observing the GPU submission actually finish -- e.g.
        // a wedged driver/validation-layer thread rather than a real GPU
        // hang (see VulkanSync.cpp). When that happens, this frame's
        // resources cannot safely be reclaimed or presented, and
        // continuing to run risks cascading into further, harder to
        // diagnose failures. Exiting immediately keeps that failure a
        // single clear log line instead of an unresponsive window or a
        // crash during device destruction.
        if (!Fluxion_RHI_WaitForFence(frameFences[frameIndex]))
        {
            FLUXION_LOG_ERROR("ForwardRendererDemo", "GPU submission did not complete in time -- exiting rather than risk using unfinished GPU resources.");
            std::exit(1);
        }
        Fluxion_RHI_ResetFence(frameFences[frameIndex]); // ready for this slot's next use, FLUXION_DEMO_FRAMES_IN_FLIGHT frames from now
        Fluxion_RHI_Swapchain_Present(swapchain, imageIndex, noSemaphore);

        // Safe to actually reclaim this frame's transient objects right
        // here, since the WaitForFence above already confirmed the GPU
        // is done with this frame's work.
        Fluxion_RHI_Device_CollectGarbage(device);
        Fluxion_RenderView_Destroy(frameView);
        Fluxion_RenderTarget_Destroy(frameTarget);
        Fluxion_RHI_DestroyTextureView(backbufferView);
        Fluxion_RenderGraph_Destroy(graph);

        frameIndex = (frameIndex + 1) % FLUXION_DEMO_FRAMES_IN_FLIGHT;
    }

    // No extra fence-drain loop needed here: this demo's frame loop is
    // already fully synchronous (every iteration's WaitForFence above
    // blocks until that iteration's own submission finishes before moving
    // on), so nothing is ever left in flight by the time the loop exits --
    // each frameFences[i] is already back in its unsignaled, no-pending-
    // work state, and waiting on it again here would just block until the
    // bounded timeout for work that was never submitted.
    FLUXION_LOG_INFO("ForwardRendererDemo", "Closing.");

    // The scene goes before the machine it runs on: destroying it runs
    // OnDestroy on everything still attached, which is script code.
    Fluxion_Scene_Destroy(scene);
    Fluxion::Script::DestroyVm(scriptVm);

    for (u32 i = 0; i < FLUXION_DEMO_FRAMES_IN_FLIGHT; ++i)
    {
        Fluxion_RHI_DestroyFence(frameFences[i]);
        Fluxion_RHI_DestroyCommandList(commandLists[i]);
    }
    Fluxion_Renderer_Destroy(renderer);
    Fluxion_RenderPipeline_Destroy(cubePipeline);
    Fluxion_MeshBuffer_Destroy(cubeMesh);
    Fluxion_Material_Destroy(cubeMaterial);
    Fluxion_ShaderProgram_Destroy(cubeProgram);
    Fluxion_RenderGraphPassRegistry_Shutdown();

    Fluxion_RHI_DestroySampler(albedoSampler);
    Fluxion_RHI_DestroyTextureView(albedoView);
    Fluxion_RHI_DestroyTexture(albedoTexture);
    Fluxion_RHI_DestroyTextureView(depthView);
    Fluxion_RHI_DestroyTexture(depthTexture);
    Fluxion_RHI_Device_CollectGarbage(device);
    Fluxion_RHI_DestroySwapchain(swapchain);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);

    Fluxion_Window_Destroy(window);
    Fluxion_WindowSystem_Shutdown();
    Fluxion_EventQueue_Destroy(&queue);
    return 0;
}
