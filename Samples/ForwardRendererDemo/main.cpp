// Manual test tool, not an automated test (same role as Samples/InputDemo):
// opens a window, brings up the Vulkan/OpenGL/D3D12 RHI backend, and every
// frame draws a rotating, textured, depth-tested 3D cube through the
// RenderCore layer (ShaderProgram/Material/MeshBuffer/RenderPipeline/
// RenderView/Renderer + a "ForwardOpaquePass" render graph node) rather
// than hand-rolled RHI calls -- a visible, on-screen demonstration that the
// whole layer actually works end to end on real hardware, across all
// three real backends, that RHITests' offscreen checks can't show on
// their own. The cube's material tint is animated every frame via
// Fluxion_Material_SetVec3 + Fluxion_Material_FlushDirty -- reached from
// Scripts/CubeRenderer.fls rather than from here, since what gets drawn
// and what colour it is are decisions this file has handed over to the
// scripts entirely. Vertex/index/
// texture data still go CPU->staging buffer->GPU_ONLY resource via
// Map/Unmap + CommandList Copy + a Barrier by hand (MeshBuffer only owns
// vertex/index buffers, not arbitrary textures, and RenderTarget/RenderView
// only wrap already-created views, they don't create textures) -- the same
// staging pattern real game code would use. Shader source is written in
// the engine's own shading language (Shaders/cube.vert.jsl,
// Shaders/cube.frag.jsl) and compiled by Fluxion_ShaderProgram_Create
// itself; this file never touches backend-specific bytecode directly.
#include <Fluxion/Application/Events/EventQueue.h>
#include <Fluxion/Application/Input/Input.h>
#include <Fluxion/Application/Time/Time.h>
#include <Fluxion/Application/Window/Window.h>
#include <Fluxion/Core/Jobs/JobSystem.h>
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
#include <Fluxion/Scene/EngineScript.hpp>
#include <Fluxion/Scene/Scene.h>
#include <Fluxion/Scene/SceneScript.hpp>
#include <Fluxion/Script/Script.hpp>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>

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

// Every script file the demo has, read fresh and handed to the compiler as
// one source: a module is compiled in one go, and a component in one file
// may perfectly well name a class declared in another. Read again on every
// reload, which is the whole point -- what is on disk now is what runs
// next.
std::string ReadDemoScripts()
{
    std::string combined;
    for (const char* scriptFile : { "/Rotator.fls", "/CubeRenderer.fls" })
    {
        std::string contents = ReadFile((std::string(FLUXION_DEMO_SCRIPT_DIR) + scriptFile).c_str());
        if (contents.empty())
        {
            FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to read %s from %s", scriptFile, FLUXION_DEMO_SCRIPT_DIR);
            return std::string();
        }
        combined += contents;
        combined += "\n";
    }
    return combined;
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

// Told apart from a real failure on purpose. A machine with no display,
// no GPU, or no driver cannot answer whether this program works, and
// reporting that as a failure would make an automated run say "your
// change is broken" when it means "not here". ctest reads this particular
// code as skipped.
static const int kExitEnvironmentCannotRun = 77;

int main(int argc, char** argv)
{
    // --graphics=vulkan (default) | --graphics=opengl | --graphics=d3d12 --
    // selects which RHI backend this demo drives. Every RenderCore call
    // below is identical regardless of backend -- ShaderProgram.cpp's own
    // CompileStage is what branches per backend now (GLSL text for
    // OpenGL, DXIL for D3D12, SPIR-V for Vulkan/Null), not this file.
    FluxionRHIBackendType backendType = FLUXION_RHI_BACKEND_VULKAN;

    // --frames=N stops after N frames and exits the same way closing the
    // window does, so an automated run reaches the shutdown path rather
    // than being killed partway through it. Everything that only happens
    // on the way out -- saving the pipeline cache, releasing GPU objects,
    // draining the job system -- is only covered if a run can end by
    // itself. 0 means run until asked to stop, which is what a person
    // wants.
    u64 frameLimit = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--graphics=opengl") == 0) backendType = FLUXION_RHI_BACKEND_OPENGL;
        else if (std::strcmp(argv[i], "--graphics=vulkan") == 0) backendType = FLUXION_RHI_BACKEND_VULKAN;
        else if (std::strcmp(argv[i], "--graphics=d3d12") == 0) backendType = FLUXION_RHI_BACKEND_D3D12;
        else if (std::strncmp(argv[i], "--frames=", 9) == 0) frameLimit = std::strtoull(argv[i] + 9, nullptr, 10);
    }
    const char* backendName = backendType == FLUXION_RHI_BACKEND_OPENGL ? "OpenGL" : backendType == FLUXION_RHI_BACKEND_D3D12 ? "D3D12" : "Vulkan";
    FluxionEventQueue queue;
    Fluxion_EventQueue_Init(&queue, NULL, 256);
    Fluxion_WindowSystem_Init(NULL, &queue, 1);

    // The input system is not fed by the window system: it is fed by
    // whoever drains the event queue, which is this file. Without both of
    // these -- the per-frame reset below and an event handed over for
    // every event popped -- Input answers "nothing is held down" forever.
    Fluxion_Input_Init();

    // The clock is started before the window is made, so the long, unrepresentative
    // stretch of work between here and the first frame is not mistaken for one.
    Fluxion_Time_Init();

    // Workers, so that recompiling a shader does not stop the picture.
    // Zero means "as many as this machine has, less the one running the
    // frame" -- the frame thread keeps its core to itself.
    Fluxion_JobSystem_Init(0, false);

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
    if (!FLUXION_HANDLE_IS_VALID(window))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "No window could be created -- this machine has no display available.");
        return kExitEnvironmentCannotRun;
    }

    FluxionRHIInstanceDesc instanceDesc = { "ForwardRendererDemo", true };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(backendType, &instanceDesc);
    if (!FLUXION_HANDLE_IS_VALID(instance))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to create a %s instance -- no usable %s loader/driver on this machine.", backendName, backendName);
        return kExitEnvironmentCannotRun;
    }

    FluxionRHIAdapterHandle adapter;
    if (Fluxion_RHI_EnumerateAdapters(instance, &adapter, 1) == 0)
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "No %s adapter found.", backendName);
        return kExitEnvironmentCannotRun;
    }
    FluxionRHIAdapterInfo adapterInfo;
    Fluxion_RHI_GetAdapterInfo(adapter, &adapterInfo);
    FLUXION_LOG_INFO("ForwardRendererDemo", "Using adapter: %s", adapterInfo.name);

    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapter, &deviceDesc);
    FluxionRHIQueueHandle graphicsQueue = Fluxion_RHI_GetQueue(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);

    // Per backend, because the file records which one wrote it and would
    // be refused anyway -- one path per backend just avoids three
    // processes fighting over the same name.
    char pipelineCachePath[128];
    std::snprintf(pipelineCachePath, sizeof(pipelineCachePath), "ForwardRendererDemo.%s.pipelinecache", backendName);

    // Before any pipeline exists. A driver's cache object is seeded at
    // creation and cannot be reseeded afterwards, so loading later would
    // silently do nothing -- the first pipeline creation is what brings
    // the cache into existence.
    if (Fluxion_RHI_Device_LoadPipelineCacheFromFile(device, pipelineCachePath))
        FLUXION_LOG_INFO("ForwardRendererDemo", "Pipeline cache loaded from %s", pipelineCachePath);
    else
        FLUXION_LOG_INFO("ForwardRendererDemo", "No usable pipeline cache at %s -- starting cold.", pipelineCachePath);

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

    // Said before the first program is made, so the first one already
    // benefits. Kept beside the build rather than anywhere shared: it
    // belongs to this build of these shaders and nothing else.
    Fluxion_ShaderProgram_SetCacheDirectory(FLUXION_DEMO_SHADER_CACHE_DIR);

    FluxionShaderProgramDesc cubeProgramDesc{};
    cubeProgramDesc.debugName = "ForwardRendererDemo.CubeProgram";
    cubeProgramDesc.vertexSource = cubeVertexSource.c_str();
    cubeProgramDesc.fragmentSource = cubeFragmentSource.c_str();
    FluxionShaderProgramHandle cubeProgram = Fluxion_ShaderProgram_Create(device, &cubeProgramDesc);
    if (!FLUXION_HANDLE_IS_VALID(cubeProgram))
    {
        // A missing shader compiler and a broken shader both end up
        // here, and they are not the same thing: one says this machine
        // cannot build the sample, the other says the sample is wrong.
        // Only the second is a failure worth reporting as one.
        if (!Fluxion::ShaderCompiler::IsDXCAvailable())
        {
            FLUXION_LOG_ERROR("ForwardRendererDemo", "No shader compiler (dxc) on this machine -- the cube's shaders cannot be built here.");
            std::exit(kExitEnvironmentCannotRun);
        }
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to create the cube ShaderProgram (see prior compile errors above).");
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

    // --- The cube as a scene object driven by script components --------
    //
    // Nothing below computes an angle, picks a colour or asks for a draw.
    // The demo makes one object, puts the two components in Scripts/ on
    // it, registers what it made under names those components ask for,
    // and then hands the scene how much time has passed each frame. What
    // ends up on the screen is whatever they have made of it.

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

    // The rest of the engine, on top of the scene: the clock, the input
    // state, and the small drawing surface a component needs to put
    // something on the screen. The scene's own types go in first, since
    // Renderer.DrawMesh is written in terms of a GameObject.
    if (!Fluxion::Scene::BuildEngineBindings(scene, sceneBindings, scriptDiagnostics))
    {
        ReportScriptDiagnostics(scriptDiagnostics);
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to describe the engine to the scripting runtime.");
        std::exit(1);
    }

    std::string demoScriptSource = ReadDemoScripts();
    if (demoScriptSource.empty()) std::exit(1);

    Fluxion::Script::CompileOptions scriptOptions;
    scriptOptions.fileName = "DemoScripts.fls";
    scriptOptions.bindings = &sceneBindings;
    scriptOptions.hostPrelude =
        std::string(Fluxion::Scene::ComponentPreludeSource()) + Fluxion::Scene::EnginePreludeSource();

    // Compiling is the slowest thing done between the window appearing and
    // the first frame, and a run that starts with the same scripts as the
    // last one need not do it at all. The directory is under the build
    // tree, so a checkout is never written to and deleting the build tree
    // is all it takes to be rid of it.
    Fluxion::Script::CompileCacheOptions scriptCache;
    scriptCache.directory = FLUXION_DEMO_SCRIPT_CACHE_DIR;

    Fluxion::Script::CompileCacheReport cacheReport;
    auto compiledScript =
        Fluxion::Script::CompileCached(demoScriptSource, scriptOptions, scriptCache, scriptDiagnostics, cacheReport);
    if (!compiledScript.IsOk())
    {
        ReportScriptDiagnostics(scriptDiagnostics);
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to compile the demo's scripts.");
        std::exit(1);
    }
    FLUXION_LOG_INFO("ForwardRendererDemo", "Scripts %s.", cacheReport.wasCached ? "loaded from the compile cache" : "compiled");

    Fluxion::Script::CompiledModule scriptModule = compiledScript.Value();
    Fluxion::Script::Vm* scriptVm = Fluxion::Script::CreateVm(scriptModule, scriptDiagnostics, &sceneBindings);
    if (!scriptVm || !Fluxion::Scene::AttachRuntime(scene, scriptVm))
    {
        ReportScriptDiagnostics(scriptDiagnostics);
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to start the scripting runtime: %s", Fluxion_Scene_GetLastError(scene));
        std::exit(1);
    }

    // The debug lines a script draws go into the same colour attachment
    // as everything else, which is a swapchain image -- and the renderer
    // cannot work its format out for itself.
    Fluxion_Renderer_SetDebugDrawColorFormat(renderer, swapchainDesc.format);
    Fluxion_Renderer_SetDebugDrawDepthFormat(renderer, depthTextureDesc.format);

    // What a script may name, and what it draws through. The three names
    // are the ones Scripts/CubeRenderer.fls asks for; making any of them
    // is a device-and-queue matter and stays here.
    Fluxion::Scene::SetScriptRenderer(renderer);
    if (!Fluxion::Scene::RegisterScriptMesh("Cube", cubeMesh) || !Fluxion::Scene::RegisterScriptMaterial("Cube", cubeMaterial) ||
        !Fluxion::Scene::RegisterScriptPipeline("Cube", cubePipeline))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to put the cube's mesh, material and pipeline within reach of a script.");
        std::exit(1);
    }

    FluxionGameObjectHandle cubeObject = Fluxion_Scene_CreateGameObject(scene, "Cube");
    Fluxion_GameObject_SetLocalPosition(scene, cubeObject, FluxionVec3{ 0.0f, 0.0f, -3.0f });

    for (const char* componentName : { "Rotator", "CubeRenderer" })
    {
        const u32 componentClass = Fluxion::Scene::FindComponentClass(scene, componentName);
        if (componentClass == Fluxion::Script::kNoClass ||
            Fluxion::Scene::AddComponent(scene, cubeObject, componentClass).IsNull())
        {
            FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to put a %s on the cube: %s", componentName,
                Fluxion_Scene_GetLastError(scene));
            std::exit(1);
        }
    }

    FLUXION_LOG_INFO("ForwardRendererDemo",
        "Window created. Space stops and starts the spin, the arrow keys and the wheel change how fast, Tab shows and hides the axes, "
        "R puts whatever is in Scripts/ now under the running cube, Escape closes.");

    bool running = true;
    u32 frameIndex = 0;

    // The reload being compiled right now, if any. One at a time: a
    // second F while the first is still going would leave the first with
    // nobody to apply it.
    FluxionShaderProgramReloadJob* shaderReload = nullptr;
    u64 framesDrawn = 0;

    // Set for the single frame that follows a resize, when the depth
    // texture has been replaced and the new one has never been used.
    bool depthIsUndefined = false;

    while (running)
    {
        // Whatever was pressed or released during the last frame stops
        // counting as "this frame" here, before any new event arrives --
        // so a component asking whether a key went down this frame is
        // asking about the frame it is running in and no other.
        Fluxion_Input_BeginFrame();

        Fluxion_WindowSystem_PollEvents();
        FluxionEvent event;
        while (Fluxion_EventQueue_Pop(&queue, &event))
        {
            // Every event goes to the input system as well as to this
            // loop's own handling of it: neither is a substitute for the
            // other, and Input ignores what it has no interest in.
            Fluxion_Input_ProcessEvent(&event);
            if (event.type == FLUXION_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
        }
        if (Fluxion_Input_WasKeyPressed(FLUXION_KEY_ESCAPE)) running = false;
        if (frameLimit != 0 && ++framesDrawn >= frameLimit) running = false;
        if (!running) break;

        // Edit a file in Scripts/, press R, and the cube goes on turning
        // -- at whatever speed the file now says, from wherever it had got
        // to. Nothing here stops the scene, and nothing here decides what
        // survives: the components keep the values they were holding, and
        // a component that had already been woken and started is not woken
        // and started again.
        //
        // Source that does not compile changes nothing at all: the message
        // names the file and the line, and the cube carries on running the
        // code it was running.
        if (Fluxion_Input_WasKeyPressed(FLUXION_KEY_R))
        {
            Fluxion::Scene::ReloadRequest reload;
            reload.source = ReadDemoScripts();
            reload.options = scriptOptions;
            reload.cache = scriptCache;

            if (reload.source.empty())
            {
                FLUXION_LOG_ERROR("ForwardRendererDemo", "Nothing was read from %s, so the scripts were left as they are.",
                    FLUXION_DEMO_SCRIPT_DIR);
            }
            else
            {
                Fluxion::Script::DiagnosticList reloadDiagnostics;
                Fluxion::Scene::ReloadReport reloadReport;
                if (Fluxion::Scene::ReloadRuntime(scene, reload, reloadDiagnostics, reloadReport))
                {
                    // The machine that was running until a moment ago. It
                    // has let go of every component the scene was holding,
                    // and nothing in this file points into it any more.
                    Fluxion::Script::DestroyVm(reloadReport.retired);
                    scriptVm = Fluxion::Scene::GetRuntime(scene);

                    FLUXION_LOG_INFO("ForwardRendererDemo", "Reloaded: %u component(s), %u field value(s) carried across, %u left behind.",
                        reloadReport.componentsCarried, reloadReport.fieldsCarried, reloadReport.fieldsDropped);
                }
                else
                {
                    ReportScriptDiagnostics(reloadDiagnostics);
                    FLUXION_LOG_ERROR("ForwardRendererDemo", "The scripts were not reloaded (%s); the cube is still running the old ones.",
                        Fluxion_Scene_GetLastError(scene));
                }
            }
        }

        // Edit a shader in Shaders/, press F, and the cube keeps drawing
        // while it compiles -- the compiling happens on a worker, and only
        // the swap waits for here, which is the one place the GPU is known
        // to be finished with the last frame.
        //
        // Two things are refused rather than half-done: source that does
        // not compile, and source whose material parameters changed. The
        // second is not a limitation of the reloading but of what a
        // material is -- it holds byte offsets from the shader it was
        // built against, and there is no honest way to reinterpret its
        // contents against a different set.
        if (shaderReload == nullptr && Fluxion_Input_WasKeyPressed(FLUXION_KEY_F))
        {
            const std::string vertexSource = ReadFile(FLUXION_DEMO_SHADER_DIR "/cube.vert.jsl");
            const std::string fragmentSource = ReadFile(FLUXION_DEMO_SHADER_DIR "/cube.frag.jsl");
            if (vertexSource.empty() || fragmentSource.empty())
            {
                FLUXION_LOG_ERROR("ForwardRendererDemo", "Nothing was read from %s, so the shaders were left as they are.",
                    FLUXION_DEMO_SHADER_DIR);
            }
            else
            {
                FluxionShaderProgramDesc reloadDesc{};
                reloadDesc.debugName = "ForwardRendererDemo.CubeProgram";
                reloadDesc.vertexSource = vertexSource.c_str();
                reloadDesc.fragmentSource = fragmentSource.c_str();

                // The sources are copied by this call, so letting the two
                // strings above go out of scope here is fine.
                shaderReload = Fluxion_ShaderProgram_BeginReload(device, cubeProgram, &reloadDesc);
                if (shaderReload == nullptr)
                    FLUXION_LOG_ERROR("ForwardRendererDemo", "The shader reload could not be started; the cube is still running the old shaders.");
            }
        }

        // Asked every frame and answered without waiting, so a compile
        // that takes a while costs nothing until it is done.
        if (shaderReload != nullptr && Fluxion_ShaderProgram_IsReloadReady(shaderReload))
        {
            const FluxionShaderProgramReloadOutcome outcome = Fluxion_ShaderProgram_FinishReload(shaderReload);
            shaderReload = nullptr;

            switch (outcome)
            {
                case FLUXION_SHADER_PROGRAM_RELOAD_OK:
                    FLUXION_LOG_INFO("ForwardRendererDemo", "Shaders reloaded.");
                    break;
                case FLUXION_SHADER_PROGRAM_RELOAD_LAYOUT_CHANGED:
                    FLUXION_LOG_ERROR("ForwardRendererDemo", "The shaders were not reloaded: their material parameters changed, which needs a restart.");
                    break;
                default:
                    FLUXION_LOG_ERROR("ForwardRendererDemo", "The shaders were not reloaded; the cube is still running the old ones.");
                    break;
            }
        }

        // How long the last frame took, worked out once and read by
        // everything below. A frame that stalled -- a debugger break, the
        // window being dragged -- is reported as the clock's ceiling
        // rather than as the truth, so the scene never takes one enormous
        // step it cannot recover from.
        Fluxion_Time_BeginFrame();

        // Acquiring comes first, and the extent is read only afterwards.
        // A backend is free to rebuild the swapchain during the acquire
        // -- that is where a resize is actually noticed -- so asking
        // beforehand answers about the swapchain that is being replaced,
        // and every size-derived decision below would be made about a
        // frame that no longer exists.
        u32 imageIndex = Fluxion_RHI_Swapchain_AcquireNextImage(swapchain, noSemaphore);

        // The swapchain's actual current image extent (queried from the
        // backend) -- NOT a separately-queried window size, which can
        // transiently disagree with the swapchain's own tracked size
        // (window resize race, OS-level border/DPI accounting) and trips
        // a hard Vulkan validation error if the render area doesn't
        // exactly match the acquired image.
        u32 surfaceWidth = 0, surfaceHeight = 0;
        Fluxion_RHI_Swapchain_GetExtent(swapchain, &surfaceWidth, &surfaceHeight);

        // A minimised window has no drawable surface, so there is neither
        // anything to render into nor a size to give the depth target.
        // Nothing was acquired in that case either, so the frame is
        // simply skipped -- presenting here would present an image that
        // was never handed out.
        if (surfaceWidth == 0 || surfaceHeight == 0)
        {
            continue;
        }

        // The depth target has to be at least as large as the area being
        // drawn into. The swapchain follows the window on its own; this
        // one does not, so a window that grew leaves it smaller than the
        // render area -- not a subtle mismatch but an outright invalid
        // draw, which Vulkan reports and stops on.
        if (surfaceWidth != depthTextureDesc.width || surfaceHeight != depthTextureDesc.height)
        {
            // Nothing is in flight here, so the old texture can go
            // straight away: this loop waits on its own fence before the
            // next iteration starts, which leaves the GPU idle at this
            // point every frame. Waiting again here would not merely be
            // redundant -- a fence that has been waited and reset is
            // pointing at the next submission, one that has not been made
            // yet, so waiting on it blocks until the timeout.
            Fluxion_RHI_DestroyTextureView(depthView);
            Fluxion_RHI_DestroyTexture(depthTexture);

            depthTextureDesc.width = surfaceWidth;
            depthTextureDesc.height = surfaceHeight;
            depthTexture = Fluxion_RHI_CreateTexture(device, &depthTextureDesc);
            depthViewDesc.texture = depthTexture;
            depthView = Fluxion_RHI_CreateTextureView(device, &depthViewDesc);

            // The new texture starts undefined, so this frame has to
            // import it as such -- see the import below. Transitioning it
            // here instead would mean a command list and a submit of its
            // own, in the middle of a frame whose pacing is built on one
            // submit per frame; the graph already emits exactly this
            // barrier for free.
            depthIsUndefined = true;

            FLUXION_LOG_INFO("ForwardRendererDemo", "Depth target resized to %ux%u", surfaceWidth, surfaceHeight);
        }

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
        // The depth texture has none of that ambiguity: at any moment
        // there is exactly one of it, and its real state is always known.
        // DEPTH_WRITE for an ordinary frame -- set by the upload command
        // list before this loop starts and left there by every Execute --
        // and UNDEFINED for the one frame after a resize replaced it,
        // because a texture that was created moments ago has never been
        // anything else. D3D12's barrier validation (unlike Vulkan's
        // UNDEFINED-is-always-valid layout) requires the declared "before"
        // state to actually match, so this is not a distinction that could
        // be skipped by always claiming one or the other.
        FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(device);
        Fluxion_RenderGraph_ImportTexture(graph, "ForwardOpaquePass.Color0", backbuffer, FLUXION_RHI_RESOURCE_STATE_UNDEFINED);
        Fluxion_RenderGraph_ImportTexture(graph, "ForwardOpaquePass.Depth", depthTexture,
            depthIsUndefined ? FLUXION_RHI_RESOURCE_STATE_UNDEFINED : FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE);
        depthIsUndefined = false;
        Fluxion_RenderGraph_AddPassFromRegistry(graph, "ForwardOpaquePass", Fluxion_Renderer_GetForwardOpaquePassUserData(renderer));

        Fluxion_Renderer_BeginFrame(renderer, frameView);

        // One turn of the scene, taken here rather than at the top of the
        // loop: a component's Update is where the cube is turned, where
        // its colour is chosen and where the draw is asked for, and a
        // draw only lands inside the frame the renderer has open.
        Fluxion_Scene_Tick(scene, Fluxion_Time_GetDeltaTime());

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

    // Nothing a script can name may outlive what it names. The renderer
    // and the three registered handles are taken away first, so that a
    // component's OnDestroy -- which runs while the scene is being
    // destroyed, below -- cannot ask for a draw into a renderer that is
    // about to be torn down.
    Fluxion::Scene::SetScriptRenderer(FluxionRendererHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 });
    Fluxion::Scene::ClearScriptAssets();

    // The scene goes before the machine it runs on: destroying it runs
    // OnDestroy on everything still attached, which is script code.
    Fluxion_Scene_Destroy(scene);
    Fluxion::Script::DestroyVm(scriptVm);

    for (u32 i = 0; i < FLUXION_DEMO_FRAMES_IN_FLIGHT; ++i)
    {
        Fluxion_RHI_DestroyFence(frameFences[i]);
        Fluxion_RHI_DestroyCommandList(commandLists[i]);
    }
    // Before anything built this run is destroyed. A backend whose cache
    // is a device-level object would not care, but one that reads the
    // pipelines themselves has nothing left to read once they are gone --
    // and it cannot report that as an error, because "no pipelines" is
    // also what a run that drew nothing looks like.
    if (Fluxion_RHI_Device_SavePipelineCacheToFile(device, pipelineCachePath))
        FLUXION_LOG_INFO("ForwardRendererDemo", "Pipeline cache saved to %s", pipelineCachePath);
    else
        FLUXION_LOG_WARN("ForwardRendererDemo", "Pipeline cache was not saved to %s", pipelineCachePath);

    Fluxion_Renderer_Destroy(renderer);
    Fluxion_RenderPipeline_Destroy(cubePipeline);
    Fluxion_MeshBuffer_Destroy(cubeMesh);
    Fluxion_Material_Destroy(cubeMaterial);
    // Applied rather than abandoned: Finish waits for the compiling and
    // then releases it, and there is no other way to let the job go.
    if (shaderReload != nullptr) Fluxion_ShaderProgram_FinishReload(shaderReload);

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
    Fluxion_JobSystem_Shutdown();
    Fluxion_WindowSystem_Shutdown();
    Fluxion_Time_Shutdown();
    Fluxion_Input_Shutdown();
    Fluxion_EventQueue_Destroy(&queue);
    return 0;
}
