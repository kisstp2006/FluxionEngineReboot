// Manual test tool, not an automated test (same role as Samples/InputDemo):
// opens a window, brings up the Vulkan or OpenGL RHI backend, and every
// frame dispatches a compute shader into a storage buffer, then draws a
// rotating, textured, depth-tested 3D cube whose fragment shading is
// modulated by that buffer -- a visible, on-screen demonstration of the
// full pipeline (compute + storage buffers + depth testing + texture
// upload) that RHITests' offscreen checks can't show on their own.
// Vertex/index/texture data all go CPU->staging buffer->GPU_ONLY resource
// via Map/Unmap + CommandList Copy + a Barrier, the same staging pattern
// real game code would use. Shader source is written in the engine's own
// shading language (Shaders/cube.vert.jsl, Shaders/cube.frag.jsl,
// Shaders/cubeBrightness.comp.jsl) and compiled at startup through
// Fluxion::ShaderCompiler -- this file never touches Vulkan-specific
// bytecode directly, only the RHI's own backend-agnostic FluxionRHIShaderDesc.
#include <Fluxion/Application/Events/EventQueue.h>
#include <Fluxion/Application/Window/Window.h>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/Platform/Time.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/ShaderCompiler/Backends/DXC/DXCAdapter.hpp>
#include <Fluxion/ShaderCompiler/ShaderCompiler.hpp>

#include <cmath>
#include <cstddef>
#include <cstdio>
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

// Runs this engine's own shading language through the shared front end
// (lex/parse/analyze/IR) and then branches on the *target* backend:
// Vulkan wants real bytecode (HLSL text -> dxc -> SPIR-V), OpenGL's
// FluxionRHIShaderDesc::bytecode field just carries GLSL source text
// directly (the RHI's OpenGL backend compiles/links it with glShaderSource/
// glCompileShader itself -- no intermediate bytecode step exists for this
// backend). Fluxion::ShaderCompiler::Compile() already produces both
// hlslSource and glslSource on every call, so no target-specific compiler
// invocation is needed here beyond picking which string to use. Aborts the
// process on failure (a startup-time shader compile error has no sensible
// runtime fallback for a minimal demo like this one).
std::vector<uint8_t> CompileShaderStage(const char* path, Fluxion::ShaderCompiler::ShaderStage stage, FluxionRHIBackendType backend)
{
    std::string source = ReadFile(path);
    if (source.empty())
    {
        FLUXION_LOG_ERROR("VulkanTriangleDemo", "Failed to read shader source: %s", path);
        std::exit(1);
    }

    Fluxion::ShaderCompiler::DiagnosticList diagnostics;
    Fluxion::ShaderCompiler::CompileOptions options;
    options.stage = stage;
    options.fileName = path;
    auto compiled = Fluxion::ShaderCompiler::Compile(source, options, diagnostics);
    if (!compiled.IsOk())
    {
        for (const auto& d : diagnostics.entries)
            std::fprintf(stderr, "  %s:%u: %s\n", d.location.file.c_str(), d.location.line, d.message.c_str());
        FLUXION_LOG_ERROR("VulkanTriangleDemo", "Shader compilation failed: %s", path);
        std::exit(1);
    }

    if (backend == FLUXION_RHI_BACKEND_OPENGL)
    {
        const std::string& glsl = compiled.Value().glslSource;
        return std::vector<uint8_t>(glsl.begin(), glsl.end());
    }

    Fluxion::ShaderCompiler::DiagnosticList dxcDiagnostics;
    auto spirv = Fluxion::ShaderCompiler::CompileToSpirv(compiled.Value().hlslSource, stage, "main", dxcDiagnostics);
    if (!spirv.IsOk())
    {
        for (const auto& d : dxcDiagnostics.entries)
            std::fprintf(stderr, "  dxc: %s\n", d.message.c_str());
        FLUXION_LOG_ERROR("VulkanTriangleDemo", "dxc SPIR-V compilation failed for: %s", path);
        std::exit(1);
    }

    return spirv.Value();
}

// --- Small local math helpers ------------------------------------------
//
// Foundation/Math.h explicitly keeps view/projection/rotation helpers out
// of FluxionMat4's own module ("those belong with a future renderer/RHI
// layer, not Foundation") -- these live here instead, as this demo's own
// renderer-shaped code. Convention: FluxionMat4::m[row][col] holds the
// standard mathematical entry M_ij (row-major *storage*, ordinary
// matrix-multiply semantics via Fluxion_Mat4_Multiply), and a shader-side
// `mvp * Vector4(position, 1.0)` treats the vector as a column vector
// (v' = M v). Both GLSL's default uniform-block matrix layout and HLSL's
// default cbuffer layout are column-major, so TransposeForUpload below
// converts our row-major-authored matrix into that column-major byte
// layout right before it's copied into the uniform buffer -- without it,
// the GPU would reconstruct the transpose of the matrix we intended.

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

FluxionMat4 MakeRotationY(f32 radians)
{
    FluxionMat4 m = Fluxion_Mat4_Identity();
    f32 c = std::cos(radians), s = std::sin(radians);
    m.m[0][0] = c; m.m[0][2] = s;
    m.m[2][0] = -s; m.m[2][2] = c;
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
    // --graphics=vulkan (default) | --graphics=opengl -- selects which RHI
    // backend this demo drives, from the same portable RHI/ShaderCompiler
    // calls either way (only CompileShaderStage's bytecode-vs-GLSL-text
    // branch above and the FLUXION_RHI_BACKEND_* passed to CreateInstance
    // below differ).
    FluxionRHIBackendType backendType = FLUXION_RHI_BACKEND_VULKAN;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--graphics=opengl") == 0) backendType = FLUXION_RHI_BACKEND_OPENGL;
        else if (std::strcmp(argv[i], "--graphics=vulkan") == 0) backendType = FLUXION_RHI_BACKEND_VULKAN;
    }
    const char* backendName = backendType == FLUXION_RHI_BACKEND_OPENGL ? "OpenGL" : "Vulkan";
    FluxionEventQueue queue;
    Fluxion_EventQueue_Init(&queue, NULL, 256);
    Fluxion_WindowSystem_Init(NULL, &queue, 1);

    // The backend is otherwise only visible in the startup log line below
    // ("Using adapter: ...") -- putting it in the title too means it's
    // visible at a glance even if the demo wasn't launched from a console.
    std::string windowTitle = std::string("Fluxion VulkanTriangleDemo [") + backendName + "] (close the window to quit)";
    FluxionWindowDesc windowDesc;
    windowDesc.title = windowTitle.c_str();
    windowDesc.width = 800;
    windowDesc.height = 600;
    windowDesc.resizable = true;
    FluxionWindowHandle window = Fluxion_Window_Create(&windowDesc);

    FluxionRHIInstanceDesc instanceDesc = { "VulkanTriangleDemo", true };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(backendType, &instanceDesc);
    if (!FLUXION_HANDLE_IS_VALID(instance))
    {
        FLUXION_LOG_ERROR("VulkanTriangleDemo", "Failed to create a %s instance -- no usable %s loader/driver on this machine.", backendName, backendName);
        return 1;
    }

    FluxionRHIAdapterHandle adapter;
    if (Fluxion_RHI_EnumerateAdapters(instance, &adapter, 1) == 0)
    {
        FLUXION_LOG_ERROR("VulkanTriangleDemo", "No %s adapter found.", backendName);
        return 1;
    }
    FluxionRHIAdapterInfo adapterInfo;
    Fluxion_RHI_GetAdapterInfo(adapter, &adapterInfo);
    FLUXION_LOG_INFO("VulkanTriangleDemo", "Using adapter: %s", adapterInfo.name);

    FluxionRHIDeviceDesc deviceDesc = { FLUXION_RHI_CAPABILITY_NONE };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapter, &deviceDesc);
    // Both Vulkan and OpenGL alias compute onto the same hardware queue as
    // graphics in this engine today (Vulkan may expose a real separate
    // compute queue depending on hardware, OpenGL never does) -- using the
    // graphics queue for the compute dispatch too is always valid per the
    // RHI's own documented queue-aliasing rules, and keeps this frame's
    // ordering trivially correct without any cross-queue synchronization.
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

    FluxionRHIBufferDesc stagingDesc = { sizeof(vertices) + sizeof(indices), FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC, FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU, "DemoStaging" };
    FluxionRHIBufferHandle stagingBuffer = Fluxion_RHI_CreateBuffer(device, &stagingDesc);
    u8* mapped = (u8*)Fluxion_RHI_MapBuffer(stagingBuffer);
    memcpy(mapped, vertices, sizeof(vertices));
    memcpy(mapped + sizeof(vertices), indices, sizeof(indices));
    Fluxion_RHI_UnmapBuffer(stagingBuffer);

    FluxionRHIBufferDesc vertexBufferDesc = { sizeof(vertices), FLUXION_RHI_BUFFER_USAGE_VERTEX_BUFFER | FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST, FLUXION_RHI_MEMORY_CLASS_GPU_ONLY, "DemoVertexBuffer" };
    FluxionRHIBufferHandle vertexBuffer = Fluxion_RHI_CreateBuffer(device, &vertexBufferDesc);
    FluxionRHIBufferDesc indexBufferDesc = { sizeof(indices), FLUXION_RHI_BUFFER_USAGE_INDEX_BUFFER | FLUXION_RHI_BUFFER_USAGE_TRANSFER_DST, FLUXION_RHI_MEMORY_CLASS_GPU_ONLY, "DemoIndexBuffer" };
    FluxionRHIBufferHandle indexBuffer = Fluxion_RHI_CreateBuffer(device, &indexBufferDesc);

    // --- Checkerboard texture: staged the same way as the geometry above,
    // then copied into a sampled texture via the new
    // Fluxion_RHI_CommandList_CopyBufferToTexture. --------------------------

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

    // --- Upload command list: vertex/index copy, texture copy, and the
    // one-time depth/texture layout transitions, all together. --------------

    FluxionRHICommandListHandle uploadCommandList = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RHI_CommandList_Begin(uploadCommandList);
    Fluxion_RHI_CommandList_CopyBuffer(uploadCommandList, stagingBuffer, 0, vertexBuffer, 0, sizeof(vertices));
    Fluxion_RHI_CommandList_CopyBuffer(uploadCommandList, stagingBuffer, sizeof(vertices), indexBuffer, 0, sizeof(indices));

    FluxionRHITextureHandle noTexture = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionRHIBarrier preCopyBarriers[2];
    preCopyBarriers[0] = FluxionRHIBarrier{ albedoTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_UNDEFINED, FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION };
    preCopyBarriers[1] = FluxionRHIBarrier{ depthTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_UNDEFINED, FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE };
    Fluxion_RHI_CommandList_Barrier(uploadCommandList, preCopyBarriers, 2);

    Fluxion_RHI_CommandList_CopyBufferToTexture(uploadCommandList, textureStagingBuffer, 0, albedoTexture, 0, 0);

    FluxionRHIBarrier postUploadBarriers[3];
    postUploadBarriers[0] = FluxionRHIBarrier{ noTexture, vertexBuffer, FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION, FLUXION_RHI_RESOURCE_STATE_VERTEX_BUFFER };
    postUploadBarriers[1] = FluxionRHIBarrier{ noTexture, indexBuffer, FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION, FLUXION_RHI_RESOURCE_STATE_INDEX_BUFFER };
    postUploadBarriers[2] = FluxionRHIBarrier{ albedoTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION, FLUXION_RHI_RESOURCE_STATE_SHADER_READ };
    Fluxion_RHI_CommandList_Barrier(uploadCommandList, postUploadBarriers, 3);
    Fluxion_RHI_CommandList_End(uploadCommandList);

    FluxionRHIFenceHandle uploadFence = Fluxion_RHI_CreateFence(device, false);
    Fluxion_RHI_Queue_Submit(graphicsQueue, &uploadCommandList, 1, uploadFence);
    Fluxion_RHI_WaitForFence(uploadFence);
    Fluxion_RHI_DestroyFence(uploadFence);
    Fluxion_RHI_DestroyCommandList(uploadCommandList);
    Fluxion_RHI_DestroyBuffer(stagingBuffer);
    Fluxion_RHI_DestroyBuffer(textureStagingBuffer);
    Fluxion_RHI_Device_CollectGarbage(device);

    // --- Frame BindGroup: the cube's MVP matrix. ---------------------------

    FluxionRHIBindGroupLayoutEntryDesc frameEntry;
    memset(&frameEntry, 0, sizeof(frameEntry));
    frameEntry.binding = 0;
    frameEntry.type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    frameEntry.visibility = FLUXION_RHI_SHADER_STAGE_FLAG_VERTEX;

    FluxionRHIBindGroupLayoutDesc frameLayoutDesc;
    memset(&frameLayoutDesc, 0, sizeof(frameLayoutDesc));
    frameLayoutDesc.entries[0] = frameEntry;
    frameLayoutDesc.entryCount = 1;
    frameLayoutDesc.debugName = "CubeFrameLayout";
    FluxionRHIBindGroupLayoutHandle frameLayout = Fluxion_RHI_CreateBindGroupLayout(device, &frameLayoutDesc);

    FluxionRHIBufferDesc mvpBufferDesc = { sizeof(FluxionMat4), FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER, FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU, "CubeMvpBuffer" };
    FluxionRHIBufferHandle mvpBuffer = Fluxion_RHI_CreateBuffer(device, &mvpBufferDesc);

    FluxionRHIBindGroupEntry frameGroupEntry;
    memset(&frameGroupEntry, 0, sizeof(frameGroupEntry));
    frameGroupEntry.binding = 0;
    frameGroupEntry.type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    frameGroupEntry.buffer = mvpBuffer;
    frameGroupEntry.bufferSize = sizeof(FluxionMat4);

    FluxionRHIBindGroupDesc frameGroupDesc;
    memset(&frameGroupDesc, 0, sizeof(frameGroupDesc));
    frameGroupDesc.layout = frameLayout;
    frameGroupDesc.entries = &frameGroupEntry;
    frameGroupDesc.entryCount = 1;
    FluxionRHIBindGroupHandle frameBindGroup = Fluxion_RHI_CreateBindGroup(device, &frameGroupDesc);

    // --- Material BindGroup: albedo texture + sampler. ----------------------

    FluxionRHIBindGroupLayoutEntryDesc materialEntries[2];
    memset(materialEntries, 0, sizeof(materialEntries));
    materialEntries[0].binding = 0;
    materialEntries[0].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    materialEntries[0].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;
    materialEntries[1].binding = 1;
    materialEntries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    materialEntries[1].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    FluxionRHIBindGroupLayoutDesc materialLayoutDesc;
    memset(&materialLayoutDesc, 0, sizeof(materialLayoutDesc));
    materialLayoutDesc.entries[0] = materialEntries[0];
    materialLayoutDesc.entries[1] = materialEntries[1];
    materialLayoutDesc.entryCount = 2;
    materialLayoutDesc.debugName = "CubeMaterialLayout";
    FluxionRHIBindGroupLayoutHandle materialLayout = Fluxion_RHI_CreateBindGroupLayout(device, &materialLayoutDesc);

    FluxionRHIBindGroupEntry materialGroupEntries[2];
    memset(materialGroupEntries, 0, sizeof(materialGroupEntries));
    materialGroupEntries[0].binding = 0;
    materialGroupEntries[0].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    materialGroupEntries[0].textureView = albedoView;
    materialGroupEntries[1].binding = 1;
    materialGroupEntries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    materialGroupEntries[1].sampler = albedoSampler;

    FluxionRHIBindGroupDesc materialGroupDesc;
    memset(&materialGroupDesc, 0, sizeof(materialGroupDesc));
    materialGroupDesc.layout = materialLayout;
    materialGroupDesc.entries = materialGroupEntries;
    materialGroupDesc.entryCount = 2;
    FluxionRHIBindGroupHandle materialBindGroup = Fluxion_RHI_CreateBindGroup(device, &materialGroupDesc);

    // --- Object BindGroup: the compute-dispatched brightness storage
    // buffer -- shared by both the compute pipeline (written) and the
    // cube's graphics pipeline (read in the fragment shader). ---------------

    FluxionRHIBindGroupLayoutEntryDesc objectEntry;
    memset(&objectEntry, 0, sizeof(objectEntry));
    objectEntry.binding = 0;
    objectEntry.type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    objectEntry.visibility = FLUXION_RHI_SHADER_STAGE_FLAG_COMPUTE | FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    FluxionRHIBindGroupLayoutDesc objectLayoutDesc;
    memset(&objectLayoutDesc, 0, sizeof(objectLayoutDesc));
    objectLayoutDesc.entries[0] = objectEntry;
    objectLayoutDesc.entryCount = 1;
    objectLayoutDesc.debugName = "CubeObjectLayout";
    FluxionRHIBindGroupLayoutHandle objectLayout = Fluxion_RHI_CreateBindGroupLayout(device, &objectLayoutDesc);

    const u32 kBrightnessElementCount = 64; // one workgroup's worth (local_size_x = 64)
    FluxionRHIBufferDesc brightnessBufferDesc = { kBrightnessElementCount * sizeof(f32), FLUXION_RHI_BUFFER_USAGE_STORAGE_BUFFER, FLUXION_RHI_MEMORY_CLASS_GPU_ONLY, "CubeBrightnessBuffer" };
    FluxionRHIBufferHandle brightnessBuffer = Fluxion_RHI_CreateBuffer(device, &brightnessBufferDesc);

    FluxionRHIBindGroupEntry objectGroupEntry;
    memset(&objectGroupEntry, 0, sizeof(objectGroupEntry));
    objectGroupEntry.binding = 0;
    objectGroupEntry.type = FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER;
    objectGroupEntry.buffer = brightnessBuffer;
    objectGroupEntry.bufferSize = brightnessBufferDesc.size;

    FluxionRHIBindGroupDesc objectGroupDesc;
    memset(&objectGroupDesc, 0, sizeof(objectGroupDesc));
    objectGroupDesc.layout = objectLayout;
    objectGroupDesc.entries = &objectGroupEntry;
    objectGroupDesc.entryCount = 1;
    FluxionRHIBindGroupHandle objectBindGroup = Fluxion_RHI_CreateBindGroup(device, &objectGroupDesc);

    // --- Cube graphics pipeline -------------------------------------------

    std::vector<uint8_t> vsSpirv = CompileShaderStage(FLUXION_DEMO_SHADER_DIR "/cube.vert.jsl", Fluxion::ShaderCompiler::ShaderStage::Vertex, backendType);
    std::vector<uint8_t> fsSpirv = CompileShaderStage(FLUXION_DEMO_SHADER_DIR "/cube.frag.jsl", Fluxion::ShaderCompiler::ShaderStage::Fragment, backendType);

    FluxionRHIShaderDesc vsDesc = { FLUXION_RHI_SHADER_STAGE_VERTEX, vsSpirv.data(), vsSpirv.size(), "main", "CubeVS" };
    FluxionRHIShaderHandle vertexShader = Fluxion_RHI_CreateShader(device, &vsDesc);
    FluxionRHIShaderDesc fsDesc = { FLUXION_RHI_SHADER_STAGE_FRAGMENT, fsSpirv.data(), fsSpirv.size(), "main", "CubeFS" };
    FluxionRHIShaderHandle fragmentShader = Fluxion_RHI_CreateShader(device, &fsDesc);

    FluxionRHIGraphicsPipelineDesc pipelineDesc;
    memset(&pipelineDesc, 0, sizeof(pipelineDesc));
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout.attributes[0].location = 0;
    pipelineDesc.vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    pipelineDesc.vertexLayout.attributes[0].offset = offsetof(FluxionDemoVertex, position);
    pipelineDesc.vertexLayout.attributes[1].location = 1;
    pipelineDesc.vertexLayout.attributes[1].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    pipelineDesc.vertexLayout.attributes[1].offset = offsetof(FluxionDemoVertex, uv);
    pipelineDesc.vertexLayout.attributeCount = 2;
    pipelineDesc.vertexLayout.stride = sizeof(FluxionDemoVertex);
    pipelineDesc.rasterState.cullMode = FLUXION_RHI_CULL_MODE_NONE;
    pipelineDesc.depthState.testEnable = true;
    pipelineDesc.depthState.writeEnable = true;
    pipelineDesc.depthState.compareOp = FLUXION_RHI_COMPARE_OP_LESS;
    pipelineDesc.topology = FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineDesc.colorFormats[0] = swapchainDesc.format;
    pipelineDesc.colorFormatCount = 1;
    pipelineDesc.depthFormat = depthTextureDesc.format;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_FRAME] = frameLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_MATERIAL] = materialLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_OBJECT] = objectLayout;
    pipelineDesc.bindGroupLayoutCount = FLUXION_RHI_BIND_GROUP_OBJECT + 1;
    pipelineDesc.debugName = "CubePipeline";
    FluxionRHIPipelineHandle pipeline = Fluxion_RHI_CreateGraphicsPipeline(device, &pipelineDesc);

    // --- Brightness compute pipeline ---------------------------------------

    std::vector<uint8_t> csSpirv = CompileShaderStage(FLUXION_DEMO_SHADER_DIR "/cubeBrightness.comp.jsl", Fluxion::ShaderCompiler::ShaderStage::Compute, backendType);
    FluxionRHIShaderDesc csDesc = { FLUXION_RHI_SHADER_STAGE_COMPUTE, csSpirv.data(), csSpirv.size(), "main", "CubeBrightnessCS" };
    FluxionRHIShaderHandle computeShader = Fluxion_RHI_CreateShader(device, &csDesc);

    FluxionRHIComputePipelineDesc computePipelineDesc;
    memset(&computePipelineDesc, 0, sizeof(computePipelineDesc));
    computePipelineDesc.computeShader = computeShader;
    computePipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_OBJECT] = objectLayout;
    computePipelineDesc.bindGroupLayoutCount = FLUXION_RHI_BIND_GROUP_OBJECT + 1;
    computePipelineDesc.debugName = "CubeBrightnessPipeline";
    FluxionRHIPipelineHandle computePipeline = Fluxion_RHI_CreateComputePipeline(device, &computePipelineDesc);

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

    FLUXION_LOG_INFO("VulkanTriangleDemo", "Window created. Dispatching a compute shader and rendering a rotating textured cube every frame.");

    bool running = true;
    u32 frameIndex = 0;
    f32 rotationAngle = 0.0f;
    while (running)
    {
        Fluxion_WindowSystem_PollEvents();
        FluxionEvent event;
        while (Fluxion_EventQueue_Pop(&queue, &event))
        {
            if (event.type == FLUXION_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
        }
        if (!running) break;

        rotationAngle += 0.01f;

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

        // Update the MVP uniform for this frame's rotation.
        f32 aspect = surfaceHeight != 0 ? (f32)surfaceWidth / (f32)surfaceHeight : 1.0f;
        FluxionMat4 projection = MakePerspective(1.0472f /* 60 degrees */, aspect, 0.1f, 100.0f);
        FluxionMat4 rotation = MakeRotationY(rotationAngle);
        FluxionMat4 translation = Fluxion_Mat4_Translation(FluxionVec3{ 0.0f, 0.0f, -3.0f });
        FluxionMat4 model = Fluxion_Mat4_Multiply(translation, rotation);
        FluxionMat4 mvp = Fluxion_Mat4_Multiply(projection, model);
        FluxionMat4 mvpForUpload = TransposeForUpload(mvp);
        void* mappedMvp = Fluxion_RHI_MapBuffer(mvpBuffer);
        memcpy(mappedMvp, &mvpForUpload, sizeof(mvpForUpload));
        Fluxion_RHI_UnmapBuffer(mvpBuffer);

        FluxionRHICommandListHandle cmd = commandLists[frameIndex];
        Fluxion_RHI_CommandList_Begin(cmd);

        // --- Compute pass: refresh the brightness storage buffer, then
        // barrier it from shader-write to shader-read before the cube's
        // fragment shader reads it later in this same command list. ---------
        Fluxion_RHI_CommandList_SetPipeline(cmd, computePipeline);
        Fluxion_RHI_CommandList_SetBindGroup(cmd, FLUXION_RHI_BIND_GROUP_OBJECT, objectBindGroup);
        Fluxion_RHI_CommandList_Dispatch(cmd, 1, 1, 1);

        FluxionRHIBarrier brightnessBarrier = { noTexture, brightnessBuffer, FLUXION_RHI_RESOURCE_STATE_SHADER_WRITE, FLUXION_RHI_RESOURCE_STATE_SHADER_READ };
        Fluxion_RHI_CommandList_Barrier(cmd, &brightnessBarrier, 1);

        FluxionRHIBarrier toRenderTarget;
        toRenderTarget.texture = backbuffer; toRenderTarget.buffer = noBuffer;
        toRenderTarget.before = FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
        toRenderTarget.after = FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET;
        Fluxion_RHI_CommandList_Barrier(cmd, &toRenderTarget, 1);

        FluxionRHIRenderingAttachment colorAttachment;
        colorAttachment.view = backbufferView;
        colorAttachment.clear = true;
        colorAttachment.clearColor[0] = 0.02f; colorAttachment.clearColor[1] = 0.02f; colorAttachment.clearColor[2] = 0.05f; colorAttachment.clearColor[3] = 1.0f;

        FluxionRHIRenderingAttachment depthAttachment;
        depthAttachment.view = depthView;
        depthAttachment.clear = true;
        depthAttachment.clearColor[0] = 1.0f; // clear depth to the far plane

        FluxionRHIRenderingDesc renderingDesc;
        renderingDesc.colorAttachments = &colorAttachment;
        renderingDesc.colorAttachmentCount = 1;
        renderingDesc.depthAttachment = &depthAttachment;
        renderingDesc.width = surfaceWidth;
        renderingDesc.height = surfaceHeight;
        Fluxion_RHI_CommandList_BeginRendering(cmd, &renderingDesc);

        Fluxion_RHI_CommandList_SetPipeline(cmd, pipeline);
        Fluxion_RHI_CommandList_SetBindGroup(cmd, FLUXION_RHI_BIND_GROUP_FRAME, frameBindGroup);
        Fluxion_RHI_CommandList_SetBindGroup(cmd, FLUXION_RHI_BIND_GROUP_MATERIAL, materialBindGroup);
        Fluxion_RHI_CommandList_SetBindGroup(cmd, FLUXION_RHI_BIND_GROUP_OBJECT, objectBindGroup);
        Fluxion_RHI_CommandList_SetVertexBuffer(cmd, 0, vertexBuffer, 0);
        Fluxion_RHI_CommandList_SetIndexBuffer(cmd, indexBuffer, 0, true);
        Fluxion_RHI_CommandList_DrawIndexed(cmd, 36, 1, 0, 0, 0);

        Fluxion_RHI_CommandList_EndRendering(cmd);

        FluxionRHIBarrier toPresent;
        toPresent.texture = backbuffer; toPresent.buffer = noBuffer;
        toPresent.before = FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET;
        toPresent.after = FLUXION_RHI_RESOURCE_STATE_PRESENT;
        Fluxion_RHI_CommandList_Barrier(cmd, &toPresent, 1);

        Fluxion_RHI_CommandList_End(cmd);

        Fluxion_RHI_Queue_Submit(graphicsQueue, &cmd, 1, frameFences[frameIndex]);

        // Fluxion_RHI_Queue_Submit has no way to signal a binary
        // semaphore (only a signalFence), so there is nothing that could
        // ever signal a present-wait semaphore. Symmetric with the
        // Acquire-side design (VulkanSwapchain.cpp CPU-blocks on an
        // internal fence instead of relying on the acquire semaphore):
        // wait for this frame's own submission to finish CPU-side before
        // presenting, so Present needs no GPU-side wait at all -- pass an
        // invalid semaphore handle, which the backend correctly turns
        // into "0 wait semaphores" rather than a present that waits on
        // something that can never be signaled.
        Fluxion_RHI_WaitForFence(frameFences[frameIndex]);
        Fluxion_RHI_ResetFence(frameFences[frameIndex]); // ready for this slot's next use, FLUXION_DEMO_FRAMES_IN_FLIGHT frames from now
        Fluxion_RHI_Swapchain_Present(swapchain, imageIndex, noSemaphore);

        // Safe to actually reclaim the retired backbuffer view right
        // here, since the WaitForFence above already confirmed the GPU
        // is done with this frame's work.
        Fluxion_RHI_Device_CollectGarbage(device);
        Fluxion_RHI_DestroyTextureView(backbufferView);

        frameIndex = (frameIndex + 1) % FLUXION_DEMO_FRAMES_IN_FLIGHT;
    }

    for (u32 i = 0; i < FLUXION_DEMO_FRAMES_IN_FLIGHT; ++i)
    {
        Fluxion_RHI_WaitForFence(frameFences[i]);
    }

    FLUXION_LOG_INFO("VulkanTriangleDemo", "Closing.");

    for (u32 i = 0; i < FLUXION_DEMO_FRAMES_IN_FLIGHT; ++i)
    {
        Fluxion_RHI_DestroyFence(frameFences[i]);
        Fluxion_RHI_DestroyCommandList(commandLists[i]);
    }
    Fluxion_RHI_DestroyPipeline(computePipeline);
    Fluxion_RHI_DestroyPipeline(pipeline);
    Fluxion_RHI_DestroyShader(computeShader);
    Fluxion_RHI_DestroyShader(vertexShader);
    Fluxion_RHI_DestroyShader(fragmentShader);
    Fluxion_RHI_DestroyBindGroup(objectBindGroup);
    Fluxion_RHI_DestroyBindGroup(materialBindGroup);
    Fluxion_RHI_DestroyBindGroup(frameBindGroup);
    Fluxion_RHI_DestroyBindGroupLayout(objectLayout);
    Fluxion_RHI_DestroyBindGroupLayout(materialLayout);
    Fluxion_RHI_DestroyBindGroupLayout(frameLayout);
    Fluxion_RHI_DestroySampler(albedoSampler);
    Fluxion_RHI_DestroyTextureView(albedoView);
    Fluxion_RHI_DestroyTexture(albedoTexture);
    Fluxion_RHI_DestroyTextureView(depthView);
    Fluxion_RHI_DestroyTexture(depthTexture);
    Fluxion_RHI_DestroyBuffer(brightnessBuffer);
    Fluxion_RHI_DestroyBuffer(mvpBuffer);
    Fluxion_RHI_DestroyBuffer(vertexBuffer);
    Fluxion_RHI_DestroyBuffer(indexBuffer);
    Fluxion_RHI_Device_CollectGarbage(device);
    Fluxion_RHI_DestroySwapchain(swapchain);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);

    Fluxion_Window_Destroy(window);
    Fluxion_WindowSystem_Shutdown();
    Fluxion_EventQueue_Destroy(&queue);
    return 0;
}
