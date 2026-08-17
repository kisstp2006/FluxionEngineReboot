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

#pragma once

// OpenGL-backend-private, shared between the Private/OpenGL/*.cpp units
// only. Compiled as C++, like the Vulkan backend.
//
// Design note: OpenGL has one implicit context per thread, so this
// backend has one active device/context at a time. WGL/GLX context
// creation needs a real window, but CreateDevice only takes an adapter --
// so CreateInstance makes a small hidden bootstrap window to host the
// context, and CreateSwapchain later retargets the SAME context onto the
// user-visible window's drawable. One GL context for the process,
// matching the backend's synchronous command-list execution.

#include "../RHIBackendVTable.h"

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Application/Window/WindowHandle.h>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <GL/glcorearb.h>
    #include <GL/wglext.h>

    // GL 1.0/1.1 entry points are real, statically-linkable exports of
    // opengl32.lib, but glcorearb.h deliberately doesn't declare them
    // (it's meant to replace GL/gl.h, not sit alongside it -- combining
    // the two produces duplicate-macro errors for shared enums like
    // GL_TRUE/GL_TRIANGLES). Declared here by hand instead, matching the
    // stable, unchanged-since-GL-1.1 opengl32.lib ABI exactly, so the
    // rest of this backend can call them directly with zero proc-address
    // lookup, same as it would on Linux (where GL/glx.h already pulls in
    // GL/gl.h transitively and these come for free).
    extern "C"
    {
        WINGDIAPI void APIENTRY glEnable(GLenum cap);
        WINGDIAPI void APIENTRY glDisable(GLenum cap);
        WINGDIAPI void APIENTRY glCullFace(GLenum mode);
        WINGDIAPI void APIENTRY glFrontFace(GLenum mode);
        WINGDIAPI void APIENTRY glPolygonMode(GLenum face, GLenum mode);
        WINGDIAPI void APIENTRY glDepthFunc(GLenum func);
        WINGDIAPI void APIENTRY glDepthMask(GLboolean flag);
        WINGDIAPI void APIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor);
        WINGDIAPI void APIENTRY glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
        WINGDIAPI void APIENTRY glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
        // The double-argument form, not glDepthRangef: this one is GL 1.0
        // and exported by opengl32.lib like everything else here, while
        // the float form arrived with GL 4.1.
        WINGDIAPI void APIENTRY glDepthRange(GLdouble nearValue, GLdouble farValue);
        WINGDIAPI void APIENTRY glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
        WINGDIAPI void APIENTRY glClear(GLbitfield mask);
        WINGDIAPI void APIENTRY glGetIntegerv(GLenum pname, GLint* params);
        WINGDIAPI const GLubyte* APIENTRY glGetString(GLenum name);
        WINGDIAPI void APIENTRY glFinish(void);
        WINGDIAPI void APIENTRY glDeleteTextures(GLsizei n, const GLuint* textures);
        WINGDIAPI void APIENTRY glPixelStorei(GLenum pname, GLint param);
    }
#else
    #include <X11/Xlib.h>
    #include <GL/glx.h>
    #include <GL/glcorearb.h>
    #include <GL/glxext.h>
#endif

#define FLUXION_RHI_OPENGL_MAX_DEVICES 2
#define FLUXION_RHI_OPENGL_MAX_ADAPTERS 1
#define FLUXION_RHI_OPENGL_MAX_COMMAND_LISTS 64
#define FLUXION_RHI_OPENGL_MAX_BUFFERS 1024
#define FLUXION_RHI_OPENGL_MAX_TEXTURES 1024
#define FLUXION_RHI_OPENGL_MAX_TEXTURE_VIEWS 1024
#define FLUXION_RHI_OPENGL_MAX_SAMPLERS 64
#define FLUXION_RHI_OPENGL_MAX_SHADERS 128
#define FLUXION_RHI_OPENGL_MAX_PIPELINES 128
#define FLUXION_RHI_OPENGL_MAX_SWAPCHAINS 4
#define FLUXION_RHI_OPENGL_MAX_SWAPCHAIN_IMAGES 1
#define FLUXION_RHI_OPENGL_MAX_FENCES 64
#define FLUXION_RHI_OPENGL_MAX_SEMAPHORES 64
#define FLUXION_RHI_OPENGL_MAX_QUERY_POOLS 16
#define FLUXION_RHI_OPENGL_MAX_BIND_GROUP_LAYOUTS 128
#define FLUXION_RHI_OPENGL_MAX_BIND_GROUPS 512
#define FLUXION_RHI_OPENGL_MAX_FRAMEBUFFERS 64

// Fixed per-group stride partitioning every BindGroup binding point into
// the global OpenGL binding-point namespace:
// actualBinding = groupIndex * FLUXION_RHIOPENGL_BINDINGS_PER_GROUP + entry.binding,
// applied identically to UBO binding points, SSBO binding points, and
// texture/sampler units (each of those is already an independent
// namespace in OpenGL, so the three never collide with each other even
// sharing the same formula).
#define FLUXION_RHIOPENGL_BINDINGS_PER_GROUP 16
#define FLUXION_RHIOPENGL_TOTAL_BINDING_SLOTS (FLUXION_RHI_MAX_BIND_GROUPS * FLUXION_RHIOPENGL_BINDINGS_PER_GROUP)

// --- Generic slot pool (same alive+generation idiom as Null/Vulkan) --------

struct FluxionRHIOpenGLSlot
{
    bool alive = false;
    u32 generation = 0;
};

bool Fluxion_RHIOpenGL_PoolAllocate(FluxionRHIOpenGLSlot* slots, u32 capacity, u32* outIndex, u32* outGeneration);
bool Fluxion_RHIOpenGL_PoolIsValid(const FluxionRHIOpenGLSlot* slots, u32 capacity, u32 index, u32 generation);
void Fluxion_RHIOpenGL_PoolFree(FluxionRHIOpenGLSlot* slots, u32 capacity, u32 index, u32 generation);

// --- State cache ----------------------------------------------------------

struct FluxionRHIOpenGLStateCache
{
    bool valid = false; // false until the first SetPipeline primes it
    FluxionRHICullMode cullMode = FLUXION_RHI_CULL_MODE_NONE;
    bool frontFaceCCW = true;
    bool wireframe = false;
    bool depthTestEnable = false;
    bool depthWriteEnable = false;
    FluxionRHICompareOp depthCompareOp = FLUXION_RHI_COMPARE_OP_ALWAYS;
    bool blendEnable = false;
};

// --- Resource-binding cache ------------------------------------------------

struct FluxionRHIOpenGLBindingCache
{
    GLuint boundUBO[FLUXION_RHIOPENGL_TOTAL_BINDING_SLOTS] = {};
    GLuint boundSSBO[FLUXION_RHIOPENGL_TOTAL_BINDING_SLOTS] = {};
    GLuint boundTexture[FLUXION_RHIOPENGL_TOTAL_BINDING_SLOTS] = {};
    GLuint boundSampler[FLUXION_RHIOPENGL_TOTAL_BINDING_SLOTS] = {};
};

// --- Device ------------------------------------------------------------------

struct FluxionRHIOpenGLDevice
{
#if defined(_WIN32)
    HWND bootstrapHwnd = nullptr;
    HDC currentHdc = nullptr;
    HGLRC context = nullptr;
    int pixelFormat = 0;
#else
    Display* display = nullptr;
    ::Window bootstrapXWindow = 0;
    ::Window currentXWindow = 0;
    GLXContext context = nullptr;
    GLXFBConfig fbConfig = nullptr;
#endif
    bool contextValid = false;

    bool debugEnabled = false;
    bool hasAnisotropicFiltering = false;
    bool hasProgramBinary = false;

    FluxionRHIOpenGLStateCache stateCache;
    FluxionRHIOpenGLBindingCache bindingCache;

    FluxionRHICapabilityFlags capabilities = 0;
    FluxionRHILimits limits = {};
};

FluxionRHIOpenGLDevice* Fluxion_RHIOpenGL_ResolveDevice(FluxionRHIDeviceHandle device);
// This backend only ever has one active device/context at a time (see the
// design note above).
FluxionRHIOpenGLDevice* Fluxion_RHIOpenGL_SoleDevice(void);

// Makes the sole device's context current against `hdc`/`xWindow` -- used
// both by the bootstrap window at device-creation time and by
// Fluxion_RHI_CreateSwapchain to retarget the same context onto the real
// window's drawable.
#if defined(_WIN32)
bool Fluxion_RHIOpenGL_MakeCurrent(FluxionRHIOpenGLDevice* deviceState, HDC hdc);
#else
bool Fluxion_RHIOpenGL_MakeCurrent(FluxionRHIOpenGLDevice* deviceState, ::Window xWindow);
#endif

// --- Loader (OpenGLLoader.cpp) ----------------------------------------------

// Creates the bootstrap window + real GL 4.5 core context, loads every GL
// 1.2+ entry point this backend uses. Returns false (deviceState left
// untouched beyond whatever partial state was created, which the caller
// tears down) on any failure.
bool Fluxion_RHIOpenGL_CreateContext(FluxionRHIOpenGLDevice* deviceState, bool enableValidation);
void Fluxion_RHIOpenGL_DestroyContext(FluxionRHIOpenGLDevice* deviceState);
void Fluxion_RHIOpenGL_LoadFunctions(void);

// --- Format mapping (OpenGLBackend.cpp) -------------------------------------

GLenum Fluxion_RHIOpenGL_MapSizedInternalFormat(FluxionRHIFormat format);
// Pixel-transfer (unpack) format+type pair for glTextureSubImage2D --
// distinct from the sized internal format above because BGRA-vs-RGBA
// channel order only matters for how CPU/buffer-side bytes are
// interpreted on the way in, not for how the texture stores them.
void Fluxion_RHIOpenGL_MapPixelTransferFormat(FluxionRHIFormat format, GLenum* outFormat, GLenum* outType);
void Fluxion_RHIOpenGL_MapVertexAttribFormat(FluxionRHIFormat format, GLenum* outType, GLint* outComponents, GLboolean* outNormalized);

// --- Resources (OpenGLResources.cpp) ----------------------------------------

struct FluxionRHIOpenGLBuffer
{
    GLuint name = 0;
    usize size = 0;
    void* mappedPointer = nullptr; // persistently mapped for CPU-visible memory classes
    bool cpuVisible = false;
};

FluxionRHIBufferHandle Fluxion_RHIOpenGL_CreateBuffer(FluxionRHIDeviceHandle device, const FluxionRHIBufferDesc* desc);
void Fluxion_RHIOpenGL_DestroyBuffer(FluxionRHIBufferHandle buffer);
void* Fluxion_RHIOpenGL_MapBuffer(FluxionRHIBufferHandle buffer);
void Fluxion_RHIOpenGL_UnmapBuffer(FluxionRHIBufferHandle buffer);
FluxionRHIOpenGLBuffer* Fluxion_RHIOpenGL_ResolveBuffer(FluxionRHIBufferHandle buffer);

// Attaches a caller-supplied name to a GL object so a capture tool shows
// "DemoAlbedoTexture" instead of a bare object id. Quietly does nothing
// when the name is NULL or the entry point was not loaded -- a name is
// diagnostic freight, never something an object's creation may fail over.
void Fluxion_RHIOpenGL_LabelObject(GLenum identifier, GLuint name, const char* label);

void Fluxion_RHIOpenGL_CommandListResetQueryPool(FluxionRHICommandListHandle commandList, FluxionRHIQueryPoolHandle queryPool, u32 firstQuery, u32 queryCount);
void Fluxion_RHIOpenGL_CommandListWriteTimestamp(FluxionRHICommandListHandle commandList, FluxionRHIQueryPoolHandle queryPool, u32 queryIndex);
bool Fluxion_RHIOpenGL_QueryPoolGetResults(FluxionRHIQueryPoolHandle queryPool, u32 firstQuery, u32 queryCount, u64* outTicks);
u64 Fluxion_RHIOpenGL_GetTimestampFrequency(FluxionRHIDeviceHandle device);

// True once the default-framebuffer sentinel texture/view has been
// handed out -- CommandList_BeginRendering
// checks this to know when to bind FBO 0 instead of building a real FBO.
struct FluxionRHIOpenGLTexture
{
    GLuint name = 0;
    GLenum target = 0;
    u32 width = 0, height = 0, depth = 1;
    u32 mipLevels = 1, arrayLayers = 1;
    FluxionRHIFormat format = FLUXION_RHI_FORMAT_UNKNOWN;
    bool isDefaultFramebufferSentinel = false;
};

FluxionRHITextureHandle Fluxion_RHIOpenGL_CreateTexture(FluxionRHIDeviceHandle device, const FluxionRHITextureDesc* desc);
void Fluxion_RHIOpenGL_DestroyTexture(FluxionRHITextureHandle texture);
FluxionRHIOpenGLTexture* Fluxion_RHIOpenGL_ResolveTexture(FluxionRHITextureHandle texture);
// Used by the swapchain to hand out its sentinel texture(s) without going
// through the normal storage-allocating Create path.
bool Fluxion_RHIOpenGL_AllocateSentinelTextureSlot(FluxionRHITextureHandle* outHandle);
void Fluxion_RHIOpenGL_FreeTextureSlotDirect(FluxionRHITextureHandle handle);

struct FluxionRHIOpenGLTextureView
{
    GLuint name = 0; // aliasing texture-view object from glTextureView, or the owning texture's own name for a trivial (whole-resource, same-format) view
    GLenum target = 0;
    FluxionRHITextureHandle texture{};
    bool ownsName = false; // true if `name` is a distinct glTextureView object this view must delete itself
};

FluxionRHITextureViewHandle Fluxion_RHIOpenGL_CreateTextureView(FluxionRHIDeviceHandle device, const FluxionRHITextureViewDesc* desc);
void Fluxion_RHIOpenGL_DestroyTextureView(FluxionRHITextureViewHandle view);
FluxionRHIOpenGLTextureView* Fluxion_RHIOpenGL_ResolveTextureView(FluxionRHITextureViewHandle view);

struct FluxionRHIOpenGLSampler
{
    GLuint name = 0;
};

FluxionRHISamplerHandle Fluxion_RHIOpenGL_CreateSampler(FluxionRHIDeviceHandle device, const FluxionRHISamplerDesc* desc);
void Fluxion_RHIOpenGL_DestroySampler(FluxionRHISamplerHandle sampler);
FluxionRHIOpenGLSampler* Fluxion_RHIOpenGL_ResolveSampler(FluxionRHISamplerHandle sampler);

// --- Binding model (OpenGLBinding.cpp) --------------------------------------

FluxionRHIBindGroupLayoutHandle Fluxion_RHIOpenGL_CreateBindGroupLayout(FluxionRHIDeviceHandle device, const FluxionRHIBindGroupLayoutDesc* desc);
void Fluxion_RHIOpenGL_DestroyBindGroupLayout(FluxionRHIBindGroupLayoutHandle layout);
const FluxionRHIBindGroupLayoutDesc* Fluxion_RHIOpenGL_ResolveBindGroupLayout(FluxionRHIBindGroupLayoutHandle layout);

struct FluxionRHIOpenGLBindGroup
{
    FluxionRHIBindGroupLayoutHandle layout{};
    FluxionRHIBindGroupEntry entries[FLUXION_RHI_MAX_BIND_GROUP_ENTRIES];
    u32 entryCount = 0;
};

FluxionRHIBindGroupHandle Fluxion_RHIOpenGL_CreateBindGroup(FluxionRHIDeviceHandle device, const FluxionRHIBindGroupDesc* desc);
void Fluxion_RHIOpenGL_DestroyBindGroup(FluxionRHIBindGroupHandle bindGroup);
FluxionRHIOpenGLBindGroup* Fluxion_RHIOpenGL_ResolveBindGroup(FluxionRHIBindGroupHandle bindGroup);
void Fluxion_RHIOpenGL_CommandListSetBindGroup(FluxionRHICommandListHandle commandList, u32 groupIndex, FluxionRHIBindGroupHandle bindGroup);

// Deleting a GL object unbinds it and frees its NAME FOR REUSE; the
// binding cache has to be told, or the recycled name compares equal to
// what it remembers and the rebind is skipped over an empty unit.
void Fluxion_RHIOpenGL_BindingCacheForgetTexture(GLuint name);
void Fluxion_RHIOpenGL_BindingCacheForgetSampler(GLuint name);
void Fluxion_RHIOpenGL_BindingCacheForgetBuffer(GLuint name);

// --- Shaders / pipelines (OpenGLPipeline.cpp) -------------------------------

struct FluxionRHIOpenGLShader
{
    GLuint name = 0;
    FluxionRHIShaderStage stage = FLUXION_RHI_SHADER_STAGE_VERTEX;
};

FluxionRHIShaderHandle Fluxion_RHIOpenGL_CreateShader(FluxionRHIDeviceHandle device, const FluxionRHIShaderDesc* desc);
void Fluxion_RHIOpenGL_DestroyShader(FluxionRHIShaderHandle shader);
FluxionRHIOpenGLShader* Fluxion_RHIOpenGL_ResolveShader(FluxionRHIShaderHandle shader);

struct FluxionRHIOpenGLPipeline
{
    GLuint program = 0;
    GLuint vao = 0; // 0 for a compute pipeline
    bool isCompute = false;
    FluxionRHIRasterState rasterState{};
    FluxionRHIDepthState depthState{};
    FluxionRHIBlendState blendState{};
    FluxionRHIPrimitiveTopology topology = FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    u32 vertexStride = 0;

    // What this VAO is currently attached to, so a frame that draws the
    // same mesh again re-attaches nothing. Skipping the redundant call
    // is not only cheaper on the CPU: a driver that sees a buffer
    // re-specified as attachment state every frame is entitled to
    // classify it as dynamic and keep it where the CPU can reach it --
    // host memory -- and then every draw pays a bus crossing for
    // geometry that never changed. Keyed on the RHI handle rather than
    // the GL name, because a destroyed buffer's name can be reissued to
    // a new buffer, and two different buffers must never compare equal.
    FluxionRHIBufferHandle attachedVertexBuffer{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    usize attachedVertexOffset = 0;
    FluxionRHIBufferHandle attachedIndexBuffer{ FLUXION_HANDLE_INVALID_INDEX, 0 };
};

FluxionRHIPipelineHandle Fluxion_RHIOpenGL_CreateGraphicsPipeline(FluxionRHIDeviceHandle device, const FluxionRHIGraphicsPipelineDesc* desc);
FluxionRHIPipelineHandle Fluxion_RHIOpenGL_CreateComputePipeline(FluxionRHIDeviceHandle device, const FluxionRHIComputePipelineDesc* desc);
void Fluxion_RHIOpenGL_DestroyPipeline(FluxionRHIPipelineHandle pipeline);
FluxionRHIOpenGLPipeline* Fluxion_RHIOpenGL_ResolvePipeline(FluxionRHIPipelineHandle pipeline);
// Index-only lookup (no generation check) for OpenGLCommandList.cpp, which
// already validated the owning FluxionRHIPipelineHandle once at
// SetPipeline time and only keeps the raw index around afterward (mirrors
// how a real command buffer would only reference an internal pipeline
// pointer, not re-check the handle on every subsequent SetVertexBuffer/
// Draw call).
FluxionRHIOpenGLPipeline* Fluxion_RHIOpenGL_ResolvePipelineByIndex(u32 index);

bool Fluxion_RHIOpenGL_SavePipelineCacheToFile(FluxionRHIDeviceHandle device, const char* path);
bool Fluxion_RHIOpenGL_LoadPipelineCacheFromFile(FluxionRHIDeviceHandle device, const char* path);

void Fluxion_RHIOpenGL_ApplyPipelineState(FluxionRHIOpenGLDevice* deviceState, const FluxionRHIOpenGLPipeline* pipeline);

// --- Command lists / framebuffer / swapchain / sync (OpenGLCommandList.cpp) -

FluxionRHICommandListHandle Fluxion_RHIOpenGL_CreateCommandList(FluxionRHIDeviceHandle device, FluxionRHIQueueType type);
void Fluxion_RHIOpenGL_DestroyCommandList(FluxionRHICommandListHandle commandList);
void Fluxion_RHIOpenGL_CommandListBegin(FluxionRHICommandListHandle commandList);
void Fluxion_RHIOpenGL_CommandListEnd(FluxionRHICommandListHandle commandList);
void Fluxion_RHIOpenGL_CommandListBeginRendering(FluxionRHICommandListHandle commandList, const FluxionRHIRenderingDesc* desc);
void Fluxion_RHIOpenGL_CommandListEndRendering(FluxionRHICommandListHandle commandList);
void Fluxion_RHIOpenGL_CommandListSetViewport(FluxionRHICommandListHandle commandList, f32 x, f32 y, f32 width, f32 height, f32 minDepth, f32 maxDepth);
void Fluxion_RHIOpenGL_CommandListSetScissor(FluxionRHICommandListHandle commandList, i32 x, i32 y, u32 width, u32 height);
void Fluxion_RHIOpenGL_CommandListSetPipeline(FluxionRHICommandListHandle commandList, FluxionRHIPipelineHandle pipeline);
void Fluxion_RHIOpenGL_CommandListSetVertexBuffer(FluxionRHICommandListHandle commandList, u32 slot, FluxionRHIBufferHandle buffer, usize offset);
void Fluxion_RHIOpenGL_CommandListSetIndexBuffer(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle buffer, usize offset, bool use16BitIndices);
void Fluxion_RHIOpenGL_CommandListDraw(FluxionRHICommandListHandle commandList, u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance);
void Fluxion_RHIOpenGL_CommandListDrawIndexed(FluxionRHICommandListHandle commandList, u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance);
void Fluxion_RHIOpenGL_CommandListDrawIndirect(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle argsBuffer, usize offset, u32 drawCount, u32 stride);
void Fluxion_RHIOpenGL_CommandListDrawIndexedIndirect(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle argsBuffer, usize offset, u32 drawCount, u32 stride);
void Fluxion_RHIOpenGL_CommandListDispatch(FluxionRHICommandListHandle commandList, u32 groupCountX, u32 groupCountY, u32 groupCountZ);
void Fluxion_RHIOpenGL_CommandListCopyBuffer(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle src, usize srcOffset, FluxionRHIBufferHandle dst, usize dstOffset, usize size);
void Fluxion_RHIOpenGL_CommandListCopyTexture(FluxionRHICommandListHandle commandList, FluxionRHITextureHandle src, FluxionRHITextureHandle dst);
void Fluxion_RHIOpenGL_CommandListCopyBufferToTexture(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle src, usize srcOffset, FluxionRHITextureHandle dst, u32 mipLevel, u32 arrayLayer);
void Fluxion_RHIOpenGL_CommandListCopyTextureToBuffer(FluxionRHICommandListHandle commandList, FluxionRHITextureHandle src, u32 mipLevel, u32 arrayLayer, FluxionRHIBufferHandle dst, usize dstOffset);
bool Fluxion_RHIOpenGL_DeviceIsFormatSupported(FluxionRHIDeviceHandle device, FluxionRHIFormat format);
void Fluxion_RHIOpenGL_CommandListBarrier(FluxionRHICommandListHandle commandList, const FluxionRHIBarrier* barriers, u32 barrierCount);
void Fluxion_RHIOpenGL_QueueSubmit(FluxionRHIQueueHandle queue, const FluxionRHICommandListHandle* commandLists, u32 commandListCount, FluxionRHIFenceHandle signalFence);

FluxionRHISwapchainHandle Fluxion_RHIOpenGL_CreateSwapchain(FluxionRHIDeviceHandle device, FluxionWindowHandle window, const FluxionRHISwapchainDesc* desc);
void Fluxion_RHIOpenGL_DestroySwapchain(FluxionRHISwapchainHandle swapchain);
u32 Fluxion_RHIOpenGL_SwapchainAcquireNextImage(FluxionRHISwapchainHandle swapchain, FluxionRHISemaphoreHandle signalSemaphore);
FluxionRHITextureHandle Fluxion_RHIOpenGL_SwapchainGetTexture(FluxionRHISwapchainHandle swapchain, u32 imageIndex);
void Fluxion_RHIOpenGL_SwapchainPresent(FluxionRHISwapchainHandle swapchain, u32 imageIndex, FluxionRHISemaphoreHandle waitSemaphore);
void Fluxion_RHIOpenGL_SwapchainGetExtent(FluxionRHISwapchainHandle swapchain, u32* outWidth, u32* outHeight);

FluxionRHIFenceHandle Fluxion_RHIOpenGL_CreateFence(FluxionRHIDeviceHandle device, bool signaled);
void Fluxion_RHIOpenGL_DestroyFence(FluxionRHIFenceHandle fence);
bool Fluxion_RHIOpenGL_WaitForFence(FluxionRHIFenceHandle fence);
void Fluxion_RHIOpenGL_ResetFence(FluxionRHIFenceHandle fence);
FluxionRHISemaphoreHandle Fluxion_RHIOpenGL_CreateSemaphore(FluxionRHIDeviceHandle device);
void Fluxion_RHIOpenGL_DestroySemaphore(FluxionRHISemaphoreHandle semaphore);
FluxionRHIQueryPoolHandle Fluxion_RHIOpenGL_CreateQueryPool(FluxionRHIDeviceHandle device, u32 queryCount);
void Fluxion_RHIOpenGL_DestroyQueryPool(FluxionRHIQueryPoolHandle queryPool);
// Shared between QueueSubmit and the Fence section, both in OpenGLCommandList.cpp.
void Fluxion_RHIOpenGL_SignalFenceIfValid(FluxionRHIFenceHandle fence);

#define FLUXION_RHIOPENGL_CHECK_CONTEXT(deviceState) FLUXION_ASSERT_MSG((deviceState) != nullptr && (deviceState)->contextValid, "Fluxion RHI OpenGL backend: no active GL context")

// GL_TEXTURE_MAX_ANISOTROPY (core since GL 4.6, same numeric value as the
// long-standing GL_TEXTURE_MAX_ANISOTROPY_EXT/_ARB) -- defined here as a
// fallback in case the vendored glcorearb.h revision predates its
// promotion to core.
#if !defined(GL_TEXTURE_MAX_ANISOTROPY)
    #define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#endif
