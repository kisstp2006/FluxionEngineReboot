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

// Manual test tool, not an automated test: draws a rotating, lit cube
// through the whole RenderCore layer on all three backends -- the
// end-to-end run that RHITests' offscreen checks cannot show.
//
// The cube is SHADED BY THE ENGINE, not by this sample:
// Shaders/cube.material.jsl only says what the surface is, and the
// scripts under Scripts/ decide what is drawn and what colour it is.
// Texture and mesh data go CPU -> staging -> GPU_ONLY by hand, the same
// pattern real game code would use.
#include <Fluxion/Application/Events/EventQueue.h>
#include <Fluxion/Assets/AssetDatabase.h>
#include <Fluxion/Assets/AssetSystem.h>
#include <Fluxion/Assets/Assets.h>
#include <Fluxion/Assets/VirtualFileSystem.h>
#include <Fluxion/Application/Input/Input.h>
#include <Fluxion/Application/Time/Time.h>
#include <Fluxion/Application/Window/Window.h>
#include <Fluxion/Core/Jobs/JobSystem.h>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Core/Service/ServiceRegistry.h>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Half.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/ScopeExit.hpp>
#include <Fluxion/Foundation/Memory/MemoryTracker.h>
#include <Fluxion/Foundation/Math.h>
#include <Fluxion/Foundation/Serialization/Stream.h>
#include <Fluxion/Platform/File.h>
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
#include <Fluxion/RenderCore/Renderer/RenderView.h>
#include <Fluxion/RenderCore/Renderer/ShadowMatrices.h>
#include <Fluxion/RenderCore/Renderer/TextureAsset.h>
#include <Fluxion/RenderCore/Renderer/Renderer.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>
#include <Fluxion/RenderCore/Renderer/TextureDefaults.h>
#include <Fluxion/Scene/EngineScript.hpp>
#include <Fluxion/Scene/Camera.h>
#include <Fluxion/Scene/Light.h>
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

// What Fluxion/Pass/Vertex.jsl declares, in the order it declares it.
//
// The normal and the tangent are not decoration: lighting needs to know
// which way the surface faces, and a normal map needs to know which way
// is sideways on it. A mesh without them can be drawn but cannot be lit,
// which is why the engine's vertex stage asks for all four.
typedef struct FluxionDemoVertex
{
    f32 position[3];
    f32 normal[3];
    f32 tangent[4]; // xyz along the surface, w the handedness of the bitangent
    f32 uv[2];
} FluxionDemoVertex;

#define FLUXION_DEMO_FRAMES_IN_FLIGHT 2

// How many of the scene's lights this sample makes room for. Not a limit
// the engine has -- the light list grows -- only how much this particular
// program brought.
#define FLUXION_DEMO_MAX_LIGHTS 16

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
    for (const char* scriptFile : { "/Rotator.fls", "/CubeRenderer.fls", "/LightOrbit.fls" })
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

// Which way a texel of a cube map faces.
//
// The face order is +X, -X, +Y, -Y, +Z, -Z, and within a face the two
// coordinates run the way every one of these backends agrees they do.
// This mapping is the thing the sky actually TESTS: get a sign wrong and
// the sky is not broken, it is turned round -- which looks like the sky
// was authored wrong rather than like the engine sampled it wrong.
FluxionVec3 CubeFaceDirection(u32 face, f32 u, f32 v)
{
    switch (face)
    {
        case 0: return FluxionVec3{ 1.0f, -v, -u };  // +X
        case 1: return FluxionVec3{ -1.0f, -v, u };  // -X
        case 2: return FluxionVec3{ u, 1.0f, v };    // +Y, straight up
        case 3: return FluxionVec3{ u, -1.0f, -v };  // -Y, straight down
        case 4: return FluxionVec3{ u, -v, 1.0f };   // +Z
        default: return FluxionVec3{ -u, -v, -1.0f }; // -Z
    }
}

// What the sky looks like in a given direction, in the same real units
// as every light in the scene -- which is why the numbers are large. The
// camera brings them down, exactly as it does for the sun.
FluxionVec3 SkyColor(FluxionVec3 direction, FluxionVec3 toSun)
{
    const FluxionVec3 d = Fluxion_Vec3_Normalize(direction);
    const f32 up = d.y;

    // Above the horizon: deep blue overhead easing to a pale warm band at
    // the horizon, which is what makes an unlit face read as sky-lit
    // rather than as grey.
    const f32 t = std::max(up, 0.0f);
    FluxionVec3 sky;
    sky.x = 14.0f + (2.0f - 14.0f) * t;
    sky.y = 18.0f + (6.0f - 18.0f) * t;
    sky.z = 26.0f + (20.0f - 26.0f) * t;

    // The sun itself, as a bright tight spot in the direction the
    // directional light comes FROM. The two agree because both are
    // written from the same vector -- a sky whose sun sat somewhere other
    // than the light would look like a shadow bug.
    const f32 alignment = d.x * toSun.x + d.y * toSun.y + d.z * toSun.z;
    if (alignment > 0.0f)
    {
        const f32 glow = std::pow(alignment, 350.0f) * 900.0f + std::pow(alignment, 12.0f) * 6.0f;
        sky.x += glow * 1.00f;
        sky.y += glow * 0.95f;
        sky.z += glow * 0.80f;
    }

    // Below the horizon: dim ground, so the underside of things is not
    // lit by a sky that does not exist down there.
    if (up < 0.0f)
    {
        const f32 ground = std::min(-up * 4.0f, 1.0f);
        sky.x = sky.x + (3.0f - sky.x) * ground;
        sky.y = sky.y + (2.6f - sky.y) * ground;
        sky.z = sky.z + (2.2f - sky.z) * ground;
    }

    return sky;
}

} // namespace

// Told apart from a real failure on purpose. A machine with no display,
// no GPU, or no driver cannot answer whether this program works, and
// reporting that as a failure would make an automated run say "your
// change is broken" when it means "not here". ctest reads this particular
// code as skipped.
static const int kExitEnvironmentCannotRun = 77;

// The eye. Named because the shadow cascades are fitted to exactly this
// shape of view, and a camera changed here without them would light the
// scene from a slab that no longer matches what is on the screen.
static const f32 kCameraFieldOfView = 1.0472f; // sixty degrees
static const f32 kCameraNearPlane = 0.1f;
static const f32 kCameraFarPlane = 100.0f;

// How the sun's cascades divide what they cover. Mostly logarithmic,
// which is the published recommendation -- see ShadowMatrices.h, which
// explains what each end of this is wrong about on its own.
static const f32 kCascadeLogarithmicShare = 0.85f;

// How much of each cascade is spent handing over to the next. Enough to
// hide the change in sharpness, little enough that most of a cascade is
// still read from one map alone.
static const f32 kCascadeHandoverShare = 0.1f;

// How many tiles the sun takes. Four, out of an atlas that holds
// sixteen: the rest are what a spot light and a point light need, and a
// sun that took them all would be the only thing in the scene casting.
static const u32 kSunCascadeCount = 4;

// How far out the sun bothers to cast. Not its camera's far plane: a
// last cascade stretched over a hundred metres would spend its texels on
// ground nobody can make out, and every nearer one would be coarser.
static const f32 kSunShadowDistance = 40.0f;

int main(int argc, char** argv)
{
    // --graphics=vulkan (default) | --graphics=opengl | --graphics=d3d12 --
    // selects which RHI backend this demo drives. Every RenderCore call
    // below is identical regardless of backend -- ShaderProgram.cpp's own
    // CompileStage is what branches per backend now (GLSL text for
    // OpenGL, DXIL for D3D12, SPIR-V for Vulkan/Null), not this file.
    FluxionRHIBackendType backendType = FLUXION_RHI_BACKEND_VULKAN;

    // --frames=N exits the same way closing the window does, so an
    // automated run covers the shutdown path instead of being killed
    // partway through it. 0 means run until asked to stop.
    u64 frameLimit = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--graphics=opengl") == 0) backendType = FLUXION_RHI_BACKEND_OPENGL;
        else if (std::strcmp(argv[i], "--graphics=vulkan") == 0) backendType = FLUXION_RHI_BACKEND_VULKAN;
        else if (std::strcmp(argv[i], "--graphics=d3d12") == 0) backendType = FLUXION_RHI_BACKEND_D3D12;
        else if (std::strncmp(argv[i], "--frames=", 9) == 0) frameLimit = std::strtoull(argv[i] + 9, nullptr, 10);
    }
    const char* backendName = backendType == FLUXION_RHI_BACKEND_OPENGL ? "OpenGL" : backendType == FLUXION_RHI_BACKEND_D3D12 ? "D3D12" : "Vulkan";
    // The host owns diagnostics subsystems, same as it owns the job
    // system: modules below only offer statistics when this is on.
    Fluxion_MemoryTracker_Init();

    // Teardown is attached to each thing as it is acquired, not written
    // once at the bottom: the bottom is only reached by the run that
    // works, and every early return used to walk out past everything
    // already started. Guards also unwind in reverse creation order for
    // free.
    FLUXION_SCOPE_EXIT(Fluxion_MemoryTracker_Shutdown());

    // The type registry, before anything makes a scene. Every object a
    // scene holds carries a transform, and the storage takes that
    // component's size from here -- so a scene cannot be made without it.
    Fluxion_Reflection_Init();
    FLUXION_SCOPE_EXIT(Fluxion_Reflection_Shutdown());

    // The asset system publishes itself here, which is how a plugin gets
    // at it without linking against it. Nothing in this sample is a
    // plugin, and it is started anyway: the asset system refuses to come
    // up without it rather than quietly leaving a build where plugins
    // simply cannot add asset types.
    Fluxion_ServiceRegistry_Init();
    FLUXION_SCOPE_EXIT(Fluxion_ServiceRegistry_Shutdown());

    FluxionEventQueue queue;
    Fluxion_EventQueue_Init(&queue, NULL, 256);
    FLUXION_SCOPE_EXIT(Fluxion_EventQueue_Destroy(&queue));

    Fluxion_WindowSystem_Init(NULL, &queue, 1);
    FLUXION_SCOPE_EXIT(Fluxion_WindowSystem_Shutdown());

    // The input system is not fed by the window system: it is fed by
    // whoever drains the event queue, which is this file. Without both of
    // these -- the per-frame reset below and an event handed over for
    // every event popped -- Input answers "nothing is held down" forever.
    Fluxion_Input_Init();
    FLUXION_SCOPE_EXIT(Fluxion_Input_Shutdown());

    // The clock is started before the window is made, so the long, unrepresentative
    // stretch of work between here and the first frame is not mistaken for one.
    Fluxion_Time_Init();
    FLUXION_SCOPE_EXIT(Fluxion_Time_Shutdown());

    // Workers, so that recompiling a shader does not stop the picture.
    // Zero means "as many as this machine has, less the one running the
    // frame" -- the frame thread keeps its core to itself.
    Fluxion_JobSystem_Init(0, false);
    FLUXION_SCOPE_EXIT(Fluxion_JobSystem_Shutdown());

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
    FLUXION_SCOPE_EXIT(Fluxion_Window_Destroy(window));

    FluxionRHIInstanceDesc instanceDesc = { "ForwardRendererDemo", true };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(backendType, &instanceDesc);
    if (!FLUXION_HANDLE_IS_VALID(instance))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to create a %s instance -- no usable %s loader/driver on this machine.", backendName, backendName);
        return kExitEnvironmentCannotRun;
    }
    FLUXION_SCOPE_EXIT(Fluxion_RHI_DestroyInstance(instance));

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
    FLUXION_SCOPE_EXIT(Fluxion_RHI_DestroyDevice(device));
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

    // The collect and the destroy travel together so that they keep the
    // order they had: whatever the device is holding back is let go
    // before the swapchain it may be holding back FOR.
    FLUXION_SCOPE_EXIT(Fluxion_RHI_Device_CollectGarbage(device); Fluxion_RHI_DestroySwapchain(swapchain));

    // --- Cube geometry: staging (CPU_TO_GPU) -> GPU_ONLY, the real upload
    // pattern, not just a directly-mapped GPU buffer. 24 vertices (4 per
    // face x 6 faces) rather than a shared 8-vertex cube, so each face
    // gets its own correct UVs. -------------------------------------------

    // Each face's own normal and tangent, given outright rather than
    // worked out from the triangles. A cube has no smooth normals to
    // average -- its corners really are corners -- and a tangent chosen to
    // follow the u direction of each face is what makes a normal map on
    // one face mean the same thing as on the next.
    static const FluxionDemoVertex vertices[24] =
    {
        // +Z (front)
        { { -0.5f, -0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
        { {  0.5f, -0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
        { {  0.5f,  0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
        { { -0.5f,  0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
        // -Z (back)
        { {  0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
        { { -0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
        { { -0.5f,  0.5f, -0.5f }, { 0.0f, 0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
        { {  0.5f,  0.5f, -0.5f }, { 0.0f, 0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
        // -X (left)
        { { -0.5f, -0.5f, -0.5f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },
        { { -0.5f, -0.5f,  0.5f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
        { { -0.5f,  0.5f,  0.5f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
        { { -0.5f,  0.5f, -0.5f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
        // +X (right)
        { {  0.5f, -0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f, 1.0f }, { 0.0f, 1.0f } },
        { {  0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f, 1.0f }, { 1.0f, 1.0f } },
        { {  0.5f,  0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f, 1.0f }, { 1.0f, 0.0f } },
        { {  0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f, 1.0f }, { 0.0f, 0.0f } },
        // +Y (top)
        { { -0.5f,  0.5f,  0.5f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
        { {  0.5f,  0.5f,  0.5f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
        { {  0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
        { { -0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
        // -Y (bottom)
        { { -0.5f, -0.5f, -0.5f }, { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
        { {  0.5f, -0.5f, -0.5f }, { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
        { {  0.5f, -0.5f,  0.5f }, { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
        { { -0.5f, -0.5f,  0.5f }, { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
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

    // The full mip chain, built on the CPU with a box filter -- without
    // mips the receding cube shimmers, and a box filter is exact for this
    // checkerboard. Levels sit in one staging buffer in the layout
    // CopyBufferToTexture's contract requires (row and placement
    // alignment).
    u32 kMipLevels = 1;
    while ((kTextureSize >> kMipLevels) >= 1) ++kMipLevels;

    std::vector<usize> mipStagingOffsets(kMipLevels);
    std::vector<std::vector<u8>> mipPixels(kMipLevels);
    mipPixels[0] = checkerPixels;
    for (u32 level = 1; level < kMipLevels; ++level)
    {
        const u32 srcSize = kTextureSize >> (level - 1);
        const u32 dstSize = kTextureSize >> level;
        mipPixels[level].resize((usize)dstSize * dstSize * 4);
        for (u32 y = 0; y < dstSize; ++y)
        {
            for (u32 x = 0; x < dstSize; ++x)
            {
                for (u32 c = 0; c < 4; ++c)
                {
                    const u32 a = mipPixels[level - 1][((y * 2) * srcSize + (x * 2)) * 4 + c];
                    const u32 b = mipPixels[level - 1][((y * 2) * srcSize + (x * 2 + 1)) * 4 + c];
                    const u32 d = mipPixels[level - 1][((y * 2 + 1) * srcSize + (x * 2)) * 4 + c];
                    const u32 e = mipPixels[level - 1][((y * 2 + 1) * srcSize + (x * 2 + 1)) * 4 + c];
                    mipPixels[level][(y * dstSize + x) * 4 + c] = (u8)((a + b + d + e + 2) / 4);
                }
            }
        }
    }

    usize stagingSizeTotal = 0;
    for (u32 level = 0; level < kMipLevels; ++level)
    {
        stagingSizeTotal = (stagingSizeTotal + FLUXION_RHI_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) / FLUXION_RHI_TEXTURE_DATA_PLACEMENT_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
        mipStagingOffsets[level] = stagingSizeTotal;
        const u32 levelSize = kTextureSize >> level;
        const usize rowBytes = ((usize)levelSize * 4 + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) / FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;
        stagingSizeTotal += rowBytes * levelSize;
    }

    FluxionRHIBufferDesc textureStagingDesc = { stagingSizeTotal, FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC, FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU, "DemoTextureStaging" };
    FluxionRHIBufferHandle textureStagingBuffer = Fluxion_RHI_CreateBuffer(device, &textureStagingDesc);
    u8* mappedTexture = (u8*)Fluxion_RHI_MapBuffer(textureStagingBuffer);
    memset(mappedTexture, 0, stagingSizeTotal);
    for (u32 level = 0; level < kMipLevels; ++level)
    {
        const u32 levelSize = kTextureSize >> level;
        const usize rowBytes = ((usize)levelSize * 4 + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) / FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;
        for (u32 y = 0; y < levelSize; ++y)
        {
            memcpy(mappedTexture + mipStagingOffsets[level] + (usize)y * rowBytes,
                mipPixels[level].data() + (usize)y * levelSize * 4,
                (usize)levelSize * 4);
        }
    }
    Fluxion_RHI_UnmapBuffer(textureStagingBuffer);

    FluxionRHITextureDesc albedoTextureDesc;
    memset(&albedoTextureDesc, 0, sizeof(albedoTextureDesc));
    albedoTextureDesc.width = kTextureSize;
    albedoTextureDesc.height = kTextureSize;
    albedoTextureDesc.depth = 1;
    albedoTextureDesc.mipLevels = kMipLevels;
    albedoTextureDesc.arrayLayers = 1;
    albedoTextureDesc.sampleCount = 1;
    albedoTextureDesc.format = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    albedoTextureDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_SAMPLED | FLUXION_RHI_TEXTURE_USAGE_TRANSFER_DST;
    albedoTextureDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    albedoTextureDesc.debugName = "DemoAlbedoTexture";
    FluxionRHITextureHandle albedoTexture = Fluxion_RHI_CreateTexture(device, &albedoTextureDesc);
    FLUXION_SCOPE_EXIT(Fluxion_RHI_DestroyTexture(albedoTexture));

    FluxionRHITextureViewDesc albedoViewDesc = { albedoTexture, albedoTextureDesc.format, 0, kMipLevels, 0, 1, FLUXION_RHI_TEXTURE_DIMENSION_2D };
    FluxionRHITextureViewHandle albedoView = Fluxion_RHI_CreateTextureView(device, &albedoViewDesc);
    FLUXION_SCOPE_EXIT(Fluxion_RHI_DestroyTextureView(albedoView));



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
    FLUXION_SCOPE_EXIT(Fluxion_RHI_DestroySampler(albedoSampler));

    // --- The sky, the long way round -------------------------------------
    //
    // Cooked to a file, entered in the database, asked for BY ID -- the
    // path a real project's sky takes; the short way would leave it all
    // untried. This file plays the importer (a real one lives in a
    // plugin), computing the sky from directions so no image reader is
    // needed.

    if (!Fluxion_AssetSystem_Init(nullptr))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "The asset system would not start.");
        return 1;
    }
    FLUXION_SCOPE_EXIT(
        // Not the reverse of how they were set up, and deliberately.
        // Shutting the asset system down releases what it still holds,
        // and releasing a texture calls its TYPE's unload -- so the type
        // has to still be registered while that happens.
        Fluxion_AssetSystem_Shutdown();
        Fluxion_TextureAsset_UnregisterType());

    // The device and the queue go in here because the load has two halves
    // and only one of them can happen on a worker: reading and decoding
    // the file, then handing the pixels to a device.
    if (!Fluxion_TextureAsset_RegisterType(device, graphicsQueue))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Could not register the texture asset type.");
        return 1;
    }

    // Thirty-two texels a face is small, and deliberately: this is a
    // gradient, and what is being shown is that a cube map is sampled by
    // direction at all, not how much detail one can hold.
    //
    // The same vector that aims the sun aims the bright spot in the sky,
    // so the two cannot drift apart -- a sun sitting somewhere other than
    // the light would read as a shadow bug.
    constexpr u32 kSkyFaceSize = 32;
    const FluxionVec3 toSun = Fluxion_Vec3_Normalize(FluxionVec3{ -0.4f, 0.7f, 0.6f });

    FluxionTextureImportSettings skySettings = Fluxion_TextureAsset_DefaultImportSettings(FLUXION_TEXTURE_USAGE_HDR);

    // Two departures from the defaults, both about this particular sky.
    //
    // No mips, because it is sampled at one level and a mip chain the
    // file did not store would be a promise these settings could not
    // keep. And not compressed: a block format's error is worst on a
    // smooth gradient, which is exactly what a sky is, and the setting
    // exists for cases like it.
    skySettings.flags = 0;
    skySettings.compression = (u32)FLUXION_TEXTURE_COMPRESSION_NONE;

    // Asked for rather than chosen: a sample that picked a format itself
    // could disagree with the settings stored beside it. The block family
    // decides nothing here, because nothing is being compressed.
    const FluxionRHIFormat skyFormat = Fluxion_TextureAsset_GetCookedFormat(
        FLUXION_TEXTURE_USAGE_HDR, (FluxionTextureCompression)skySettings.compression,
        FLUXION_TEXTURE_BLOCK_FAMILY_BC);

    // Tightly packed, six faces of one level, which is what a cooked file
    // holds. The padding a device wants is put in when it is uploaded,
    // and that happens inside the asset type rather than here.
    const usize skyPixelBytes =
        Fluxion_TextureAsset_GetTotalByteSize(skyFormat, kSkyFaceSize, kSkyFaceSize, 1, FLUXION_RHI_CUBE_FACE_COUNT);
    std::vector<u8> skyPixels(skyPixelBytes, 0);
    {
        FluxionHalf* texel = reinterpret_cast<FluxionHalf*>(skyPixels.data());
        for (u32 face = 0; face < FLUXION_RHI_CUBE_FACE_COUNT; ++face)
        {
            for (u32 y = 0; y < kSkyFaceSize; ++y)
            {
                for (u32 x = 0; x < kSkyFaceSize; ++x)
                {
                    // The centre of the texel, not its corner: sampling
                    // the corner shifts the whole sky by half a texel,
                    // which at this size is visible along the seams.
                    const f32 u = ((f32)x + 0.5f) / (f32)kSkyFaceSize * 2.0f - 1.0f;
                    const f32 v = ((f32)y + 0.5f) / (f32)kSkyFaceSize * 2.0f - 1.0f;

                    const FluxionVec3 color = SkyColor(CubeFaceDirection(face, u, v), toSun);
                    *texel++ = Fluxion_Half_FromFloat(color.x);
                    *texel++ = Fluxion_Half_FromFloat(color.y);
                    *texel++ = Fluxion_Half_FromFloat(color.z);
                    *texel++ = Fluxion_Half_FromFloat(1.0f);
                }
            }
        }
    }

    FluxionTextureAssetData skyData;
    memset(&skyData, 0, sizeof(skyData));
    skyData.width = kSkyFaceSize;
    skyData.height = kSkyFaceSize;
    skyData.mipCount = 1;
    skyData.arrayLayers = FLUXION_RHI_CUBE_FACE_COUNT;
    skyData.format = skyFormat;

    // The shape travels with the pixels. Six layers on their own are six
    // pictures; what makes them a thing that can be sampled by direction
    // is this, and it is stored rather than guessed from the count.
    skyData.dimension = FLUXION_RHI_TEXTURE_DIMENSION_CUBE;
    skyData.pixels = skyPixels.data();
    skyData.pixelBytes = skyPixels.size();

    std::vector<u8> skyCooked(skyPixels.size() + 1024, 0);
    FluxionStream skyWriter;
    Fluxion_MemoryStream_InitWriter(&skyWriter, skyCooked.data(), skyCooked.size());
    if (!Fluxion_TextureAsset_Write(&skyWriter, &skyData) || Fluxion_Stream_HasOverflowed(&skyWriter))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Could not cook the sky.");
        return 1;
    }
    skyCooked.resize(Fluxion_Stream_GetPosition(&skyWriter));

    // Under the build tree rather than beside the sources, for the same
    // reason the shader and script caches are: a checkout is never
    // written to, and deleting the build tree is all it takes to be rid
    // of what has accumulated here.
    if (!Fluxion_Platform_DirectoryExists(FLUXION_DEMO_ASSET_DIR) &&
        !Fluxion_Platform_DirectoryCreate(FLUXION_DEMO_ASSET_DIR))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Could not make the folder the cooked assets go in: %s", FLUXION_DEMO_ASSET_DIR);
        return 1;
    }

    // The scheme, not the folder, is what everything downstream names. A
    // built game mounts a package under the same one, and nothing that
    // asks for a file has to be changed.
    if (!Fluxion_Vfs_Mount("assets", Fluxion_VfsDirectorySource_Create(FLUXION_DEMO_ASSET_DIR)))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Could not mount the cooked assets.");
        return 1;
    }

    if (!Fluxion_Vfs_WriteAll("assets://Sky.fluxtex", skyCooked.data(), skyCooked.size()))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Could not write the cooked sky.");
        return 1;
    }

    FluxionAssetDesc skyAsset;
    memset(&skyAsset, 0, sizeof(skyAsset));
    skyAsset.type = Fluxion_TextureAsset_TypeId();

    // For reading logs, and nothing else. What resolves the sky is the id
    // the database hands back below.
    skyAsset.name = "Sky";
    skyAsset.cookedPath = "assets://Sky.fluxtex";
    skyAsset.version = 1;

    // Stored beside it so that a re-import would produce the same file.
    // The database keeps the bytes and hashes them; it does not read
    // them, and could not -- a plugin's own asset type brings its own
    // settings through the same field.
    skyAsset.importSettings = &skySettings;
    skyAsset.importSettingsSize = (u32)sizeof(skySettings);

    FluxionUUID skyId;
    if (!Fluxion_AssetDatabase_Add(&skyAsset, &skyId))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Could not enter the sky in the asset database.");
        return 1;
    }

    // From the settings, not written out again here. Which way a texture
    // wraps is part of what it IS -- an environment sampled past its edge
    // would wrap round to the far side of the sky -- and the settings are
    // where that is decided.
    const FluxionRHISamplerDesc skySamplerDesc = Fluxion_TextureAsset_GetSamplerDesc(&skySettings);
    FluxionRHISamplerHandle skySampler = Fluxion_RHI_CreateSampler(device, &skySamplerDesc);
    FLUXION_SCOPE_EXIT(Fluxion_RHI_DestroySampler(skySampler));

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
    FLUXION_SCOPE_EXIT(Fluxion_RHI_DestroyTexture(depthTexture));

    FluxionRHITextureViewDesc depthViewDesc = { depthTexture, depthTextureDesc.format, 0, 1, 0, 1, FLUXION_RHI_TEXTURE_DIMENSION_2D };
    FluxionRHITextureViewHandle depthView = Fluxion_RHI_CreateTextureView(device, &depthViewDesc);
    FLUXION_SCOPE_EXIT(Fluxion_RHI_DestroyTextureView(depthView));

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
    cubeMeshDesc.vertexLayout.attributes[1].format = FLUXION_RHI_FORMAT_R32G32B32_FLOAT;
    cubeMeshDesc.vertexLayout.attributes[1].offset = offsetof(FluxionDemoVertex, normal);
    cubeMeshDesc.vertexLayout.attributes[2].location = 2;
    cubeMeshDesc.vertexLayout.attributes[2].format = FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT;
    cubeMeshDesc.vertexLayout.attributes[2].offset = offsetof(FluxionDemoVertex, tangent);
    cubeMeshDesc.vertexLayout.attributes[3].location = 3;
    cubeMeshDesc.vertexLayout.attributes[3].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    cubeMeshDesc.vertexLayout.attributes[3].offset = offsetof(FluxionDemoVertex, uv);
    // The locations are not free to choose: they are the order
    // Fluxion/Pass/Vertex.jsl declares its inputs in, and a mesh that
    // numbered them differently would hand the normal to whatever asked
    // for the tangent.
    cubeMeshDesc.vertexLayout.attributeCount = 4;
    cubeMeshDesc.vertexLayout.stride = sizeof(FluxionDemoVertex);
    cubeMeshDesc.bounds = FluxionAABB{ FluxionVec3{ -0.5f, -0.5f, -0.5f }, FluxionVec3{ 0.5f, 0.5f, 0.5f } };
    cubeMeshDesc.debugName = "ForwardRendererDemo.Cube";
    FluxionMeshBufferHandle cubeMesh = Fluxion_MeshBuffer_Create(device, graphicsQueue, &cubeMeshDesc);
    if (!FLUXION_HANDLE_IS_VALID(cubeMesh))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to create the cube MeshBuffer.");
        return 1;
    }
    FLUXION_SCOPE_EXIT(Fluxion_MeshBuffer_Destroy(cubeMesh));

    FluxionRHICommandListHandle uploadCommandList = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RHI_CommandList_Begin(uploadCommandList);

    FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionRHIBarrier preCopyBarriers[2];
    preCopyBarriers[0] = FluxionRHIBarrier{ albedoTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_UNDEFINED, FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION };
    preCopyBarriers[1] = FluxionRHIBarrier{ depthTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_UNDEFINED, FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE };
    Fluxion_RHI_CommandList_Barrier(uploadCommandList, preCopyBarriers, 2);

    for (u32 level = 0; level < kMipLevels; ++level)
    {
        Fluxion_RHI_CommandList_CopyBufferToTexture(uploadCommandList, textureStagingBuffer, mipStagingOffsets[level], albedoTexture, level, 0);
    }

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
    FLUXION_SCOPE_EXIT(Fluxion_RenderGraphPassRegistry_Shutdown());

    // The material says what the surface is; the engine says what becomes
    // of it. Neither stage below is written here -- the vertex half is the
    // engine's for every pass, and the fragment half is the material's
    // source with one pass appended to it. The same file, appended with
    // the depth pass instead, is the depth shader.
    std::string cubeMaterialSource = ReadFile(FLUXION_DEMO_SHADER_DIR "/cube.material.jsl");
    if (cubeMaterialSource.empty())
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to read cube.material.jsl from %s", FLUXION_DEMO_SHADER_DIR);
        return 1;
    }

    char* builtVertexSource = Fluxion_MaterialShader_BuildVertexSource(FLUXION_MATERIAL_PASS_FORWARD);
    char* builtFragmentSource = Fluxion_MaterialShader_BuildFragmentSource(cubeMaterialSource.c_str(), FLUXION_MATERIAL_PASS_FORWARD);
    FLUXION_SCOPE_EXIT(Fluxion_MaterialShader_FreeSource(builtVertexSource); Fluxion_MaterialShader_FreeSource(builtFragmentSource));
    if (builtVertexSource == nullptr || builtFragmentSource == nullptr)
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to compose the cube's shader stages from its material source.");
        return 1;
    }

    // Said before the first program is made, so the first one already
    // benefits. Kept beside the build rather than anywhere shared: it
    // belongs to this build of these shaders and nothing else.
    Fluxion_ShaderProgram_SetCacheDirectory(FLUXION_DEMO_SHADER_CACHE_DIR);

    FluxionShaderProgramDesc cubeProgramDesc{};
    cubeProgramDesc.debugName = "ForwardRendererDemo.CubeProgram";
    cubeProgramDesc.vertexSource = builtVertexSource;
    cubeProgramDesc.fragmentSource = builtFragmentSource;
    FluxionShaderProgramHandle cubeProgram = Fluxion_ShaderProgram_Create(device, &cubeProgramDesc);
    FLUXION_SCOPE_EXIT(if (FLUXION_HANDLE_IS_VALID(cubeProgram)) Fluxion_ShaderProgram_Destroy(cubeProgram));
    if (!FLUXION_HANDLE_IS_VALID(cubeProgram))
    {
        // A missing shader compiler and a broken shader both end up
        // here, and they are not the same thing: one says this machine
        // cannot build the sample, the other says the sample is wrong.
        // Only the second is a failure worth reporting as one.
        if (!Fluxion::ShaderCompiler::IsDXCAvailable())
        {
            FLUXION_LOG_ERROR("ForwardRendererDemo", "No shader compiler (dxc) on this machine -- the cube's shaders cannot be built here.");
            return kExitEnvironmentCannotRun;
        }
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to create the cube ShaderProgram (see prior compile errors above).");

        // A return rather than an exit, so the guards above run. Walking
        // out of the process instead unwinds nothing, and what that costs
        // is invisible until something is watching for it.
        return 1;
    }

    FluxionMaterialHandle cubeMaterial = Fluxion_Material_Create(device, cubeProgram);
    if (!FLUXION_HANDLE_IS_VALID(cubeMaterial))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to create the cube Material.");
        return 1;
    }
    FLUXION_SCOPE_EXIT(Fluxion_Material_Destroy(cubeMaterial));
    // What the surface is made of, said in the terms the engine
    // understands. These are not names this sample invented: they are the
    // parameters Fluxion/Material.jsl declares, which is why the engine
    // can offer a setter for each one and refuse a material that declares
    // one of them as something else.
    Fluxion_Material_SetBaseColor(cubeMaterial, FluxionVec4{ 1.0f, 1.0f, 1.0f, 1.0f });
    Fluxion_Material_SetMetallic(cubeMaterial, 0.0f);
    Fluxion_Material_SetRoughness(cubeMaterial, 0.4f);

    // The four percent nearly every ordinary material reflects head-on.
    // Without it every non-metal would reflect identically and water,
    // cloth and glass would be the same thing at a glancing angle.
    Fluxion_Material_SetReflectance(cubeMaterial, 0.5f);
    Fluxion_Material_SetEmissive(cubeMaterial, FluxionVec3{ 0.0f, 0.0f, 0.0f });
    Fluxion_Material_SetNormalScale(cubeMaterial, 1.0f);
    Fluxion_Material_SetOcclusionStrength(cubeMaterial, 1.0f);
    Fluxion_Material_SetAlphaMode(cubeMaterial, FLUXION_MATERIAL_ALPHA_OPAQUE);

    // The standard material samples five maps, and a slot left empty is
    // not an empty slot -- it is an unbound texture, which is a broken
    // draw on some backends and black on others. The engine keeps a
    // one-texel stand-in for each kind so that a material need only
    // provide the maps it actually has.
    if (!Fluxion_TextureDefaults_Init(device, graphicsQueue))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to create the engine's stand-in textures.");
        return 1;
    }
    FLUXION_SCOPE_EXIT(Fluxion_TextureDefaults_Shutdown());

    Fluxion_Material_SetTextureSlot(cubeMaterial, FLUXION_MATERIAL_TEXTURE_BASE_COLOR, albedoView, albedoSampler);
    Fluxion_Material_SetTextureSlot(cubeMaterial, FLUXION_MATERIAL_TEXTURE_METALLIC_ROUGHNESS,
        Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), albedoSampler);
    Fluxion_Material_SetTextureSlot(cubeMaterial, FLUXION_MATERIAL_TEXTURE_NORMAL,
        Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_FLAT_NORMAL), albedoSampler);
    Fluxion_Material_SetTextureSlot(cubeMaterial, FLUXION_MATERIAL_TEXTURE_OCCLUSION,
        Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), albedoSampler);
    Fluxion_Material_SetTextureSlot(cubeMaterial, FLUXION_MATERIAL_TEXTURE_EMISSIVE,
        Fluxion_TextureDefaults_GetView(FLUXION_DEFAULT_TEXTURE_WHITE), albedoSampler);
    Fluxion_Material_FlushDirty(cubeMaterial);

    FluxionRenderPipelineHandle cubePipeline = Fluxion_RenderPipeline_Create(device, cubeProgram, FLUXION_RENDER_PIPELINE_CATEGORY_OPAQUE, swapchainDesc.format, depthTextureDesc.format);
    if (!FLUXION_HANDLE_IS_VALID(cubePipeline))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to create the cube RenderPipeline.");
        return 1;
    }
    FLUXION_SCOPE_EXIT(Fluxion_RenderPipeline_Destroy(cubePipeline));

    FluxionRendererHandle renderer = Fluxion_Renderer_Create(device, graphicsQueue);
    if (!FLUXION_HANDLE_IS_VALID(renderer))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to create the FluxionRenderer.");
        return 1;
    }
    FLUXION_SCOPE_EXIT(Fluxion_Renderer_Destroy(renderer));

    // --- Per-frame-in-flight resources (caller-managed, no hidden
    // backend FrameContext) --------------------------------------------------

    FluxionRHICommandListHandle commandLists[FLUXION_DEMO_FRAMES_IN_FLIGHT];
    FluxionRHIFenceHandle frameFences[FLUXION_DEMO_FRAMES_IN_FLIGHT];
    for (u32 i = 0; i < FLUXION_DEMO_FRAMES_IN_FLIGHT; ++i)
    {
        commandLists[i] = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
        frameFences[i] = Fluxion_RHI_CreateFence(device, true);
    }
    FLUXION_SCOPE_EXIT(for (u32 i = 0; i < FLUXION_DEMO_FRAMES_IN_FLIGHT; ++i) {
        Fluxion_RHI_DestroyFence(frameFences[i]);
        Fluxion_RHI_DestroyCommandList(commandLists[i]);
    });
    // No semaphore for Acquire/Present: Acquire already CPU-blocks on an
    // internal fence, Present is preceded by WaitForFence below, and an
    // unused binary semaphore accumulates signals nothing consumes --
    // which Vulkan rejects on the second frame.
    FluxionRHISemaphoreHandle noSemaphore = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    // --- The cube as a scene object driven by script components --------
    //
    // Nothing below computes an angle, picks a colour or asks for a draw.
    // The demo makes one object, puts the two components in Scripts/ on
    // it, registers what it made under names those components ask for,
    // and then hands the scene how much time has passed each frame. What
    // ends up on the screen is whatever they have made of it.

    // Declared here rather than where it is started, so the guard below
    // can reach it: the scene and the runtime it runs on come apart in an
    // order that is not the reverse of the order they were made in, and
    // that is easier to say once than to keep true in several places.
    Fluxion::Script::Vm* scriptVm = nullptr;

    FluxionSceneHandle scene = Fluxion_Scene_Create();
    if (!Fluxion_Scene_IsValid(scene))
    {
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to create the scene.");
        return 1;
    }
    FLUXION_SCOPE_EXIT(
        // Said before the scene is destroyed, so that a script
        // component's OnDestroy -- which runs while the scene is being
        // destroyed, below -- cannot ask for a draw into a renderer that
        // is about to be torn down.
        Fluxion::Scene::SetScriptRenderer(FluxionRendererHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 });
        Fluxion::Scene::ClearScriptAssets();

        // The scene goes before the machine it runs on.
        Fluxion_Scene_Destroy(scene);
        if (scriptVm != nullptr) Fluxion::Script::DestroyVm(scriptVm);
    );

    Fluxion::Script::BindingTable sceneBindings;
    Fluxion::Script::DiagnosticList scriptDiagnostics;
    if (!Fluxion::Scene::BuildBindingTable(scene, sceneBindings, scriptDiagnostics))
    {
        ReportScriptDiagnostics(scriptDiagnostics);
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to describe the scene to the scripting runtime.");
        return 1;
    }

    // The rest of the engine, on top of the scene: the clock, the input
    // state, and the small drawing surface a component needs to put
    // something on the screen. The scene's own types go in first, since
    // Renderer.DrawMesh is written in terms of a GameObject.
    if (!Fluxion::Scene::BuildEngineBindings(scene, sceneBindings, scriptDiagnostics))
    {
        ReportScriptDiagnostics(scriptDiagnostics);
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to describe the engine to the scripting runtime.");
        return 1;
    }

    std::string demoScriptSource = ReadDemoScripts();
    if (demoScriptSource.empty()) return 1;

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
        return 1;
    }
    FLUXION_LOG_INFO("ForwardRendererDemo", "Scripts %s.", cacheReport.wasCached ? "loaded from the compile cache" : "compiled");

    Fluxion::Script::CompiledModule scriptModule = compiledScript.Value();
    scriptVm = Fluxion::Script::CreateVm(scriptModule, scriptDiagnostics, &sceneBindings);
    if (!scriptVm || !Fluxion::Scene::AttachRuntime(scene, scriptVm))
    {
        ReportScriptDiagnostics(scriptDiagnostics);
        FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to start the scripting runtime: %s", Fluxion_Scene_GetLastError(scene));
        return 1;
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
        return 1;
    }

    // The sun, as an object: it can be moved, turned off, saved and read
    // back, and a second one is a second object rather than a renderer
    // change. Large numbers on purpose -- sunlight IS enormous, and the
    // camera's exposure is what brings it back down.
    FluxionGameObjectHandle sunObject = Fluxion_Scene_CreateGameObject(scene, "Sun");
    {
        FluxionDirectionalLight sun{};
        sun.color = FluxionVec3{ 230.0f, 220.0f, 200.0f }; // slightly warm, as daylight is
        if (Fluxion_GameObject_AddComponent(scene, sunObject, Fluxion_DirectionalLight_TypeId(), &sun) == nullptr)
        {
            FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to put a directional light on the sun object.");
            return 1;
        }

        // Which way it faces is the object's business, not the light's.
        // Aimed down and to the right, so it comes from over the camera's
        // left shoulder and the cube's faces catch it at different angles
        // as it turns -- which is the whole visible difference between a
        // lit surface and a flat one.
        Fluxion_GameObject_SetLocalRotation(scene, sunObject, Fluxion_Quat_LookRotation(FluxionVec3{ 0.4f, -0.7f, -0.6f }));
    }

    // A point light that goes round the cube, and a spot light aimed up
    // at it from below.
    //
    // Three kinds of light in one scene, which is what makes the falloff
    // and the cone visible rather than merely present. The moving one is
    // moved by a script -- nothing in this program says where it is at
    // any moment, only that it goes round.
    FluxionGameObjectHandle orbitObject = Fluxion_Scene_CreateGameObject(scene, "OrbitLight");
    {
        FluxionPointLight orbit{};

        // Strongly coloured, so which light is lighting which face is
        // obvious. Bright, for the same reason the sun is: these are real
        // amounts of light and the camera brings them down.
        orbit.color = FluxionVec3{ 120.0f, 30.0f, 10.0f };

        // Where it stops mattering. Chosen a little beyond the far side
        // of the cube, so the falloff reaching zero is something that
        // happens on screen rather than off it.
        orbit.range = 6.0f;
        if (Fluxion_GameObject_AddComponent(scene, orbitObject, Fluxion_PointLight_TypeId(), &orbit) == nullptr)
        {
            FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to put a point light on the orbiting object.");
            return 1;
        }
    }

    FluxionGameObjectHandle spotObject = Fluxion_Scene_CreateGameObject(scene, "UnderLight");
    {
        FluxionSpotLight spot{};
        spot.color = FluxionVec3{ 10.0f, 40.0f, 90.0f };
        spot.range = 8.0f;

        // A tight cone with a soft edge: the gap between the two angles
        // IS the softness, and equal angles would give a hard rim.
        spot.innerConeAngle = 0.25f;
        spot.outerConeAngle = 0.45f;
        if (Fluxion_GameObject_AddComponent(scene, spotObject, Fluxion_SpotLight_TypeId(), &spot) == nullptr)
        {
            FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to put a spot light on the under-light object.");
            return 1;
        }

        Fluxion_GameObject_SetLocalPosition(scene, spotObject, FluxionVec3{ 0.0f, -2.2f, -3.0f });
        Fluxion_GameObject_SetLocalRotation(scene, spotObject, Fluxion_Quat_LookRotation(FluxionVec3{ 0.0f, 1.0f, 0.0f }));
    }

    // The sky is named by the scene, not by this file: a component
    // holding an ID, not a texture handle. Its position is never read --
    // an environment surrounds everything. Being an object means it can
    // be saved, packaged, and set by something other than this program.
    {
        FluxionGameObjectHandle skyObject = Fluxion_Scene_CreateGameObject(scene, "Sky");

        FluxionEnvironmentLight environment{};
        environment.environment.asset = skyId;
        environment.intensity = 1.0f;
        if (Fluxion_GameObject_AddComponent(scene, skyObject, Fluxion_EnvironmentLight_TypeId(), &environment) == nullptr)
        {
            FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to put the environment on the sky object.");
            return 1;
        }
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
            return 1;
        }
    }

    // Something for the shadow to land on.
    //
    // The same mesh as the cube, flattened and moved down: a shadow needs
    // a surface under the thing casting it, and a second mesh would prove
    // nothing this one does not. It draws through the same component, so
    // it is in the same list the shadow pass walks -- the cube casts, the
    // floor receives, and neither is told which it is.
    {
        FluxionGameObjectHandle floorObject = Fluxion_Scene_CreateGameObject(scene, "Floor");
        Fluxion_GameObject_SetLocalPosition(scene, floorObject, FluxionVec3{ 0.0f, -1.5f, -3.0f });
        Fluxion_GameObject_SetLocalScale(scene, floorObject, FluxionVec3{ 20.0f, 0.1f, 20.0f });

        const u32 componentClass = Fluxion::Scene::FindComponentClass(scene, "CubeRenderer");
        if (componentClass == Fluxion::Script::kNoClass ||
            Fluxion::Scene::AddComponent(scene, floorObject, componentClass).IsNull())
        {
            FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to put a renderer on the floor: %s",
                Fluxion_Scene_GetLastError(scene));
            return 1;
        }
    }

    // The eye, as an object -- sixty degrees, at the origin, looking down
    // negative Z, which is what the fixed camera of the old demo was.
    // Being an object means a script could move it exactly as one moves
    // the orbiting light.
    {
        FluxionGameObjectHandle cameraObject = Fluxion_Scene_CreateGameObject(scene, "Camera");
        FluxionCamera camera{};
        camera.fovYRadians = kCameraFieldOfView;
        camera.nearPlane = kCameraNearPlane;
        camera.farPlane = kCameraFarPlane;
        if (Fluxion_GameObject_AddComponent(scene, cameraObject, Fluxion_Camera_TypeId(), &camera) == nullptr)
        {
            FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to put the camera on its object.");
            return 1;
        }
    }

    // The script that moves the light knows nothing about lights: it
    // moves the object it is on, and that object happens to carry one.
    // The same script on a camera would orbit a camera.
    {
        const u32 orbitClass = Fluxion::Scene::FindComponentClass(scene, "LightOrbit");
        if (orbitClass == Fluxion::Script::kNoClass ||
            Fluxion::Scene::AddComponent(scene, orbitObject, orbitClass).IsNull())
        {
            FLUXION_LOG_ERROR("ForwardRendererDemo", "Failed to put a LightOrbit on the orbiting light: %s",
                Fluxion_Scene_GetLastError(scene));
            return 1;
        }
    }

    FLUXION_LOG_INFO("ForwardRendererDemo",
        "Window created. Space stops and starts the spin, L stops and starts the orbiting light, the arrow keys and the wheel change how fast, Tab shows and hides the axes, "
        "R puts whatever is in Scripts/ now under the running cube, Escape closes.");

    bool running = true;
    u32 frameIndex = 0;

    // Said once, not once a frame.
    bool reportedTooManyLights = false;

    // The reload being compiled right now, if any. One at a time: a
    // second F while the first is still going would leave the first with
    // nobody to apply it.
    FluxionShaderProgramReloadJob* shaderReload = nullptr;
    // Applied rather than abandoned: Finish waits for the compiling and
    // then releases it, and there is no other way to let the job go.
    FLUXION_SCOPE_EXIT(if (shaderReload != nullptr) Fluxion_ShaderProgram_FinishReload(shaderReload));
    u64 framesDrawn = 0;

    // Two timestamps a frame -- one before the graph executes, one after
    // -- read back after the same fence wait the frame already does, so
    // the values are certainly complete. The sum and count make a
    // closing average: a real number a person can compare against what
    // a frame of this scene plausibly costs.
    FluxionRHIQueryPoolHandle gpuTimeQueries = Fluxion_RHI_CreateQueryPool(device, 2);
    FLUXION_SCOPE_EXIT(Fluxion_RHI_DestroyQueryPool(gpuTimeQueries));
    const u64 gpuTimestampFrequency = Fluxion_RHI_Device_GetTimestampFrequency(device);
    f64 gpuTimeTotalMs = 0.0;
    u64 gpuTimeSamples = 0;

    // Set for the single frame that follows a resize, when the depth
    // texture has been replaced and the new one has never been used.
    bool depthIsUndefined = false;

    // Held across frames rather than acquired inside one: an asset takes
    // more than a frame to arrive, and asking again each time would start
    // the load over and never get there.
    FluxionAssetHandle environmentAsset = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_SCOPE_EXIT(if (FLUXION_HANDLE_IS_VALID(environmentAsset)) Fluxion_Assets_Release(environmentAsset));

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
        // from wherever it had got to -- components keep their values,
        // and one already started is not started again. Source that does
        // not compile changes nothing: the message names file and line,
        // and the old code keeps running.
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

        // Edit a shader in Shaders/, press F: compiling happens on a
        // worker, only the swap waits here. Refused rather than half-done:
        // source that does not compile, and source whose material
        // parameters changed -- a material holds byte offsets from the
        // shader it was built against.
        if (shaderReload == nullptr && Fluxion_Input_WasKeyPressed(FLUXION_KEY_F))
        {
            // Only the material file is read. The two stages are put
            // together again the same way they were at startup, so a
            // reload picks up a change to the engine's own passes as
            // readily as a change to this material -- and neither of
            // them is written out twice for the reloading to fall out of
            // step with.
            const std::string materialSource = ReadFile(FLUXION_DEMO_SHADER_DIR "/cube.material.jsl");
            char* reloadVertex = materialSource.empty() ? nullptr : Fluxion_MaterialShader_BuildVertexSource(FLUXION_MATERIAL_PASS_FORWARD);
            char* reloadFragment = materialSource.empty() ? nullptr : Fluxion_MaterialShader_BuildFragmentSource(materialSource.c_str(), FLUXION_MATERIAL_PASS_FORWARD);

            if (reloadVertex == nullptr || reloadFragment == nullptr)
            {
                FLUXION_LOG_ERROR("ForwardRendererDemo", "Nothing was read from %s, so the shaders were left as they are.",
                    FLUXION_DEMO_SHADER_DIR);
            }
            else
            {
                FluxionShaderProgramDesc reloadDesc{};
                reloadDesc.debugName = "ForwardRendererDemo.CubeProgram";
                reloadDesc.vertexSource = reloadVertex;
                reloadDesc.fragmentSource = reloadFragment;

                // The sources are copied by this call, so releasing the
                // two below is fine.
                shaderReload = Fluxion_ShaderProgram_BeginReload(device, cubeProgram, &reloadDesc);
                if (shaderReload == nullptr)
                    FLUXION_LOG_ERROR("ForwardRendererDemo", "The shader reload could not be started; the cube is still running the old shaders.");
            }

            Fluxion_MaterialShader_FreeSource(reloadVertex);
            Fluxion_MaterialShader_FreeSource(reloadFragment);
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

        FluxionRHITextureViewDesc backbufferViewDesc = { backbuffer, swapchainDesc.format, 0, 1, 0, 1, FLUXION_RHI_TEXTURE_DIMENSION_2D };
        FluxionRHITextureViewHandle backbufferView = Fluxion_RHI_CreateTextureView(device, &backbufferViewDesc);

        FluxionRenderTargetDesc targetDesc{};
        targetDesc.colorViews[0] = backbufferView;
        targetDesc.colorViewCount = 1;
        targetDesc.depthView = depthView;
        FluxionRenderTargetHandle frameTarget = Fluxion_RenderTarget_Create(device, &targetDesc);

        // The eye is an object in the scene, like the lights: asked for
        // every frame, because something can move it. Only the aspect
        // comes from here -- it belongs to the surface, not the scene.
        f32 aspect = surfaceHeight != 0 ? (f32)surfaceWidth / (f32)surfaceHeight : 1.0f;
        FluxionMat4 cameraView = Fluxion_Mat4_Identity();
        FluxionMat4 cameraProjection = Fluxion_Mat4_Identity();
        if (!Fluxion_Scene_GatherCamera(scene, aspect, &cameraView, &cameraProjection))
        {
            FLUXION_LOG_ERROR("ForwardRendererDemo", "The scene has no camera to look through.");
            running = false;
            continue;
        }

        FluxionRenderViewDesc viewDesc{};
        viewDesc.viewMatrix = cameraView;
        viewDesc.projectionMatrix = cameraProjection;
        viewDesc.viewport = FluxionViewport{ 0.0f, 0.0f, (f32)surfaceWidth, (f32)surfaceHeight, 0.0f, 1.0f };
        viewDesc.scissor = FluxionScissorRect{ 0, 0, surfaceWidth, surfaceHeight };
        viewDesc.renderTarget = frameTarget;
        viewDesc.layerMask = 0xFFFFFFFFu;

        // A few percent of the sun, arriving from everywhere, so the
        // faces turned away are dim rather than black. It stands in for a
        // sky until there is one.
        viewDesc.ambientColor = FluxionVec3{ 8.0f, 9.0f, 12.0f };

        // The camera. An exposure of one would be a guess; these are the
        // settings a photographer would dial for a scene lit this
        // brightly, and the engine works the multiplier out from them.
        viewDesc.exposure = Fluxion_Exposure_FromCamera(2.0f, 1.0f / 60.0f, 400.0f);
        viewDesc.tonemapWhitePoint = 4.0f;

        // The swapchain here is an ordinary eight-bit format with no
        // curve of its own, so the pass has to encode for the display.
        // Written down beside the format that made it true.
        viewDesc.encodeOutputToSRGB = true;
        FluxionRenderViewHandle frameView = Fluxion_RenderView_Create(device, &viewDesc);

        // The lights, read out of the scene rather than written here.
        //
        // Every frame, and after the scene has been ticked, because a
        // light is on an object now: something can move it, turn it off,
        // or add another, and none of that reaches the picture unless
        // this is asked again.
        FluxionRenderLight sceneLights[FLUXION_DEMO_MAX_LIGHTS];
        const u32 lightsInScene = Fluxion_Scene_GatherLights(scene, sceneLights, FLUXION_DEMO_MAX_LIGHTS);
        const u32 lightsUsed = lightsInScene < FLUXION_DEMO_MAX_LIGHTS ? lightsInScene : FLUXION_DEMO_MAX_LIGHTS;
        if (lightsInScene > FLUXION_DEMO_MAX_LIGHTS && !reportedTooManyLights)
        {
            // Said once rather than every frame, and said at all: a light
            // silently left out is a scene that is darker than it was
            // built to be, with nothing to point at.
            FLUXION_LOG_WARN("ForwardRendererDemo", "The scene has %u lights and this sample makes room for %u -- the rest are not lit.",
                lightsInScene, (u32)FLUXION_DEMO_MAX_LIGHTS);
            reportedTooManyLights = true;
        }
        Fluxion_RenderView_SetLights(frameView, sceneLights, lightsUsed);

        // --- What each light casts ---------------------------------------
        //
        // Three kinds, three shapes. The sun gets cascades, because it
        // covers everything and the detail has to go where the pixels
        // are; the spot gets the one map its cone needs; the point light
        // gets six, because it shines every way at once. They go in
        // together, and the atlas hands out what it has -- a light that
        // does not fit casts nothing rather than something broken.
        {
            u32 atlasSize = 0;
            u32 tileSize = 0;
            Fluxion_RenderView_GetShadowAtlasSize(frameView, &atlasSize, &tileSize);

            FluxionVec3 eye{};
            FluxionVec3 forward{};
            Fluxion_Mat4_DecomposeView(cameraView, &eye, &forward);

            FluxionRenderViewShadow shadows[FLUXION_RENDER_VIEW_MAX_SHADOWS] = {};
            u32 shadowCount = 0;

            for (u32 lightIndex = 0; lightIndex < lightsUsed; ++lightIndex)
            {
                const FluxionRenderLight& light = sceneLights[lightIndex];

                if (light.type == FLUXION_RENDER_LIGHT_DIRECTIONAL)
                {
                    if (shadowCount + kSunCascadeCount > FLUXION_RENDER_VIEW_MAX_SHADOWS) continue;

                    f32 splits[FLUXION_SHADOW_MAX_CASCADES + 1] = {};
                    if (!Fluxion_ShadowMatrices_CascadeSplits(kCameraNearPlane, kSunShadowDistance, kSunCascadeCount,
                                                             kCascadeLogarithmicShare, splits))
                    {
                        continue;
                    }

                    for (u32 i = 0; i < kSunCascadeCount; ++i)
                    {
                        f32 radius = 0.0f;
                        const FluxionVec3 centre = Fluxion_ShadowMatrices_CascadeSphere(
                            eye, forward, kCameraFieldOfView, aspect, splits[i], splits[i + 1], &radius);

                        FluxionRenderViewShadow* shadow = &shadows[shadowCount++];
                        shadow->lightViewProjection = Fluxion_ShadowMatrices_Directional(
                            light.direction, centre, radius, tileSize);
                        shadow->lightIndex = lightIndex;
                        shadow->coverTo = splits[i + 1];

                        // Handed over across the last part of each
                        // cascade rather than at a line.
                        shadow->blendBand = (splits[i + 1] - splits[i]) * kCascadeHandoverShare;

                        Fluxion_ShadowMatrices_DirectionalBias(radius, tileSize, &shadow->depthBias, &shadow->normalBias);
                    }
                }
                else if (light.type == FLUXION_RENDER_LIGHT_SPOT)
                {
                    if (shadowCount + 1 > FLUXION_RENDER_VIEW_MAX_SHADOWS) continue;

                    FluxionRenderViewShadow* shadow = &shadows[shadowCount++];
                    // Back to an angle. A light carries the cosine
                    // because that is what shading wants every pixel; a
                    // projection wants the angle itself, and this is the
                    // one place a frame pays for turning it back.
                    shadow->lightViewProjection = Fluxion_ShadowMatrices_Spot(
                        light.position, light.direction, std::acos(light.outerConeCos), light.range);
                    shadow->lightIndex = lightIndex;

                    // One map, so nothing to hand over to and nothing to
                    // stop covering -- the cone's own falloff already
                    // ends where the shadow would.
                    shadow->coverTo = kCameraFarPlane;

                    Fluxion_ShadowMatrices_PerspectiveBias(light.range, tileSize, &shadow->depthBias, &shadow->normalBias);
                }
                else
                {
                    if (shadowCount + FLUXION_RHI_CUBE_FACE_COUNT > FLUXION_RENDER_VIEW_MAX_SHADOWS) continue;

                    for (u32 face = 0; face < FLUXION_RHI_CUBE_FACE_COUNT; ++face)
                    {
                        FluxionRenderViewShadow* shadow = &shadows[shadowCount++];
                        shadow->lightViewProjection = Fluxion_ShadowMatrices_PointFace(light.position, face, light.range);
                        shadow->lightIndex = lightIndex;
                        shadow->coverTo = kCameraFarPlane;
                        shadow->cubeFaces = true;

                        Fluxion_ShadowMatrices_PerspectiveBias(light.range, tileSize, &shadow->depthBias, &shadow->normalBias);
                    }
                }
            }

            Fluxion_RenderView_SetShadows(frameView, shadows, shadowCount);
        }

        // --- The sky, resolved from what the scene asked for -------------
        //
        // Every frame, and not once at startup, because what the scene
        // asks for can change while it runs -- and because an asset is
        // not ready the moment it is asked for. The first few frames of
        // this demo draw no sky at all, and that is the normal state of
        // affairs rather than a failure.
        FluxionEnvironmentLight sceneEnvironment{};
        const bool sceneHasEnvironment =
            Fluxion_Scene_GatherEnvironment(scene, &sceneEnvironment) && Fluxion_AssetRef_IsSet(sceneEnvironment.environment);

        if (sceneHasEnvironment)
        {
            // Asked for again only when it is a DIFFERENT one. Acquiring
            // the same id every frame would be correct -- the asset
            // system hands back one asset with two holders -- but it
            // would also take a reference every frame and give none back.
            const bool alreadyHeld =
                FLUXION_HANDLE_IS_VALID(environmentAsset) &&
                Fluxion_UUID_Equals(Fluxion_Assets_GetId(environmentAsset), sceneEnvironment.environment.asset);

            if (!alreadyHeld)
            {
                if (FLUXION_HANDLE_IS_VALID(environmentAsset)) Fluxion_Assets_Release(environmentAsset);
                environmentAsset = Fluxion_Assets_AcquireRef(sceneEnvironment.environment);
            }
        }

        // The half of a load that cannot happen anywhere else: handing
        // what a worker decoded to the device this thread owns.
        Fluxion_Assets_Update();

        if (FLUXION_HANDLE_IS_VALID(environmentAsset) &&
            Fluxion_Assets_GetState(environmentAsset) == FLUXION_ASSET_STATE_READY)
        {
            const FluxionTextureAsset* sky = (const FluxionTextureAsset*)Fluxion_Assets_GetObject(environmentAsset);

            // The sky behind the cube and the reflections on it read the
            // same texture, which is the point of it being one.
            Fluxion_RenderView_SetEnvironment(frameView, sky->view, skySampler, sceneEnvironment.intensity);
        }

        Fluxion_RenderView_UpdateFrameConstants(frameView);

        FluxionRHICommandListHandle cmd = commandLists[frameIndex];
        Fluxion_RHI_CommandList_Begin(cmd);

        // Inside the recording, before anything draws with this view: the
        // buffer a shader reads is GPU-only, so the list reaches it as a
        // recorded copy rather than as a write.
        Fluxion_RenderView_UploadLighting(frameView, cmd);

        // The sky into the nine numbers the surfaces read. Does nothing
        // on a frame whose environment did not change, which is every
        // frame after the first.
        Fluxion_Renderer_UpdateEnvironment(renderer, frameView, cmd);

        // The backbuffer is ALWAYS imported as UNDEFINED: with two images
        // in flight, a "first frame" flag lies about image B, and
        // UNDEFINED ("discard what was there") is always a valid before-
        // state -- fine here, since the pass clears both attachments.
        //
        // The depth texture is the opposite: there is exactly one, its
        // state is always known (DEPTH_WRITE normally, UNDEFINED the one
        // frame after a resize), and D3D12 validates that the declared
        // before-state really matches -- so the distinction cannot be
        // skipped.
        FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(device);
        Fluxion_RenderGraph_ImportTexture(graph, "ForwardOpaquePass.Color0", backbuffer, FLUXION_RHI_RESOURCE_STATE_UNDEFINED);
        Fluxion_RenderGraph_ImportTexture(graph, "ForwardOpaquePass.Depth", depthTexture,
            depthIsUndefined ? FLUXION_RHI_RESOURCE_STATE_UNDEFINED : FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE);
        depthIsUndefined = false;

        // The atlas, imported as something to read: the view cleared it
        // once before the first frame and every frame since has left it
        // that way round, so this is where it really is -- and D3D12
        // checks that the state declared here is the state it is in.
        Fluxion_RenderGraph_ImportTexture(graph, FLUXION_RENDER_VIEW_SHADOW_ATLAS_RESOURCE,
            Fluxion_RenderView_GetShadowAtlasTexture(frameView), FLUXION_RHI_RESOURCE_STATE_SHADER_READ);

        // Added before the pass that reads it, though the order here is
        // not what decides: each pass says what it touches, and the graph
        // works the rest out. Both are added whether or not anything
        // casts a shadow this frame -- see their Setup functions.
        Fluxion_RenderGraph_AddPassFromRegistry(graph, "ShadowPass", Fluxion_Renderer_GetForwardOpaquePassUserData(renderer));
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
        Fluxion_RHI_CommandList_ResetQueryPool(cmd, gpuTimeQueries, 0, 2);
        Fluxion_RHI_CommandList_WriteTimestamp(cmd, gpuTimeQueries, 0);
        Fluxion_RenderGraph_Execute(graph, cmd);
        Fluxion_Renderer_EndFrame(renderer, cmd);
        Fluxion_RHI_CommandList_WriteTimestamp(cmd, gpuTimeQueries, 1);

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

        // WaitForFence can time out (bounded, not infinite -- e.g. a
        // wedged driver thread). Then this frame's resources cannot be
        // safely reclaimed or presented, so exiting at once keeps the
        // failure one clear log line instead of a cascade.
        if (!Fluxion_RHI_WaitForFence(frameFences[frameIndex]))
        {
            FLUXION_LOG_ERROR("ForwardRendererDemo", "GPU submission did not complete in time -- exiting rather than risk using unfinished GPU resources.");
            std::exit(1);
        }
        Fluxion_RHI_ResetFence(frameFences[frameIndex]); // ready for this slot's next use, FLUXION_DEMO_FRAMES_IN_FLIGHT frames from now

        // The fence above covered this frame's submission, so both
        // timestamps exist by now on every backend.
        u64 gpuTicks[2];
        if (gpuTimestampFrequency != 0 && Fluxion_RHI_QueryPool_GetResults(gpuTimeQueries, 0, 2, gpuTicks) && gpuTicks[1] > gpuTicks[0])
        {
            gpuTimeTotalMs += (f64)(gpuTicks[1] - gpuTicks[0]) * 1000.0 / (f64)gpuTimestampFrequency;
            ++gpuTimeSamples;
        }
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

    // No fence-drain loop: the frame loop is fully synchronous, so
    // nothing is in flight when it exits -- and waiting again on an
    // already-reset fence blocks until the timeout for work that was
    // never submitted.
    FLUXION_LOG_INFO("ForwardRendererDemo", "Closing.");

    // Before anything built this run is destroyed. A backend whose cache
    // is a device-level object would not care, but one that reads the
    // pipelines themselves has nothing left to read once they are gone --
    // and it cannot report that as an error, because "no pipelines" is
    // also what a run that drew nothing looks like.
    if (Fluxion_RHI_Device_SavePipelineCacheToFile(device, pipelineCachePath))
        FLUXION_LOG_INFO("ForwardRendererDemo", "Pipeline cache saved to %s", pipelineCachePath);
    else
        FLUXION_LOG_WARN("ForwardRendererDemo", "Pipeline cache was not saved to %s", pipelineCachePath);

    if (gpuTimeSamples > 0)
    {
        FLUXION_LOG_INFO("ForwardRendererDemo", "GPU frame time: %.3f ms on average over %llu frames.",
            gpuTimeTotalMs / (f64)gpuTimeSamples, (unsigned long long)gpuTimeSamples);
    }
    // Everything above is let go by the guards, in the reverse of the
    // order it was made. The memory tracker is last of all, and so its
    // report -- a warning per domain with bytes still standing -- is
    // taken once nothing else is left holding any.
    return 0;
}
