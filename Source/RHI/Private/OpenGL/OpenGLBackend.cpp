// OpenGL RHI backend entry point: instance/adapter enumeration, device
// (= real WGL/GLX context) creation, queue resolution, format mapping, and
// the vtable that dispatches every other Fluxion_RHI_* call into the rest
// of Private/OpenGL/*.cpp.
//
// Design note: see the comment block at the top of OpenGLCommon.h for why
// device creation bootstraps its own hidden native window rather than
// requiring one up front -- Fluxion_RHI_CreateDevice's signature (adapter
// + desc only) has no window parameter, but WGL/GLX context creation
// fundamentally needs a real HDC/Drawable to create against. This backend
// only ever reports one adapter ("the current OpenGL implementation");
// real adapter info (renderer/vendor strings, GL_MAX_* limits) is only
// knowable once a context exists, so Fluxion_RHI_EnumerateAdapters hands
// out a placeholder handle and Fluxion_RHI_GetAdapterInfo lazily creates
// (and keeps, for later reuse as the real device context) a temporary
// bootstrap context the first time info is requested, if a device hasn't
// already been created.

#include "OpenGLCommon.h"
#include "OpenGLFunctions.h"

#include <cstring>

// --- Generic slot pool -------------------------------------------------------

bool Fluxion_RHIOpenGL_PoolAllocate(FluxionRHIOpenGLSlot* slots, u32 capacity, u32* outIndex, u32* outGeneration)
{
    for (u32 i = 0; i < capacity; ++i)
    {
        if (!slots[i].alive)
        {
            slots[i].alive = true;
            *outIndex = i;
            *outGeneration = slots[i].generation;
            return true;
        }
    }
    return false;
}

bool Fluxion_RHIOpenGL_PoolIsValid(const FluxionRHIOpenGLSlot* slots, u32 capacity, u32 index, u32 generation)
{
    return index < capacity && slots[index].alive && slots[index].generation == generation;
}

void Fluxion_RHIOpenGL_PoolFree(FluxionRHIOpenGLSlot* slots, u32 capacity, u32 index, u32 generation)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(slots, capacity, index, generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI OpenGL backend: destroy called with an invalid or already-destroyed handle");
        return;
    }
    slots[index].alive = false;
    ++slots[index].generation;
}

// --- Instance / adapter / device state ---------------------------------------

static bool s_wantValidation = false;

static FluxionRHIOpenGLSlot s_deviceSlots[FLUXION_RHI_OPENGL_MAX_DEVICES];
static FluxionRHIOpenGLDevice s_devices[FLUXION_RHI_OPENGL_MAX_DEVICES];
static FluxionRHIOpenGLDevice* s_activeDevice = nullptr;

FluxionRHIOpenGLDevice* Fluxion_RHIOpenGL_ResolveDevice(FluxionRHIDeviceHandle device)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_deviceSlots, FLUXION_RHI_OPENGL_MAX_DEVICES, device.index, device.generation)) return nullptr;
    return &s_devices[device.index];
}

FluxionRHIOpenGLDevice* Fluxion_RHIOpenGL_SoleDevice(void) { return s_activeDevice; }

// --- Format mapping ------------------------------------------------------

GLenum Fluxion_RHIOpenGL_MapSizedInternalFormat(FluxionRHIFormat format)
{
    switch (format)
    {
        case FLUXION_RHI_FORMAT_R8G8B8A8_UNORM: return GL_RGBA8;
        case FLUXION_RHI_FORMAT_R8G8B8A8_SRGB: return GL_SRGB8_ALPHA8;
        // OpenGL sized internal formats only describe bit layout, not
        // channel order -- BGRA vs RGBA only matters for the pixel-
        // transfer `format` argument of an upload call, not for storage
        // allocation, so both map to the same internal format here.
        case FLUXION_RHI_FORMAT_B8G8R8A8_UNORM: return GL_RGBA8;
        case FLUXION_RHI_FORMAT_B8G8R8A8_SRGB: return GL_SRGB8_ALPHA8;
        case FLUXION_RHI_FORMAT_R32_FLOAT: return GL_R32F;
        case FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT: return GL_RGBA16F;
        case FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT: return GL_RGBA32F;
        case FLUXION_RHI_FORMAT_R32G32B32_FLOAT: return GL_RGB32F;
        case FLUXION_RHI_FORMAT_R32G32_FLOAT: return GL_RG32F;
        case FLUXION_RHI_FORMAT_D32_FLOAT: return GL_DEPTH_COMPONENT32F;
        case FLUXION_RHI_FORMAT_D24_UNORM_S8_UINT: return GL_DEPTH24_STENCIL8;
        default: return GL_RGBA8;
    }
}

void Fluxion_RHIOpenGL_MapPixelTransferFormat(FluxionRHIFormat format, GLenum* outFormat, GLenum* outType)
{
    switch (format)
    {
        case FLUXION_RHI_FORMAT_R8G8B8A8_UNORM:
        case FLUXION_RHI_FORMAT_R8G8B8A8_SRGB:
            *outFormat = GL_RGBA; *outType = GL_UNSIGNED_BYTE; return;
        case FLUXION_RHI_FORMAT_B8G8R8A8_UNORM:
        case FLUXION_RHI_FORMAT_B8G8R8A8_SRGB:
            *outFormat = GL_BGRA; *outType = GL_UNSIGNED_BYTE; return;
        case FLUXION_RHI_FORMAT_R32_FLOAT:
            *outFormat = GL_RED; *outType = GL_FLOAT; return;
        case FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT:
        case FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT:
            *outFormat = GL_RGBA; *outType = GL_FLOAT; return;
        case FLUXION_RHI_FORMAT_R32G32B32_FLOAT:
            *outFormat = GL_RGB; *outType = GL_FLOAT; return;
        case FLUXION_RHI_FORMAT_R32G32_FLOAT:
            *outFormat = GL_RG; *outType = GL_FLOAT; return;
        default:
            *outFormat = GL_RGBA; *outType = GL_UNSIGNED_BYTE; return;
    }
}

void Fluxion_RHIOpenGL_MapVertexAttribFormat(FluxionRHIFormat format, GLenum* outType, GLint* outComponents, GLboolean* outNormalized)
{
    switch (format)
    {
        case FLUXION_RHI_FORMAT_R32_FLOAT: *outType = GL_FLOAT; *outComponents = 1; *outNormalized = GL_FALSE; return;
        case FLUXION_RHI_FORMAT_R32G32_FLOAT: *outType = GL_FLOAT; *outComponents = 2; *outNormalized = GL_FALSE; return;
        case FLUXION_RHI_FORMAT_R32G32B32_FLOAT: *outType = GL_FLOAT; *outComponents = 3; *outNormalized = GL_FALSE; return;
        case FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT: *outType = GL_FLOAT; *outComponents = 4; *outNormalized = GL_FALSE; return;
        case FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT: *outType = GL_HALF_FLOAT; *outComponents = 4; *outNormalized = GL_FALSE; return;
        case FLUXION_RHI_FORMAT_R8G8B8A8_UNORM:
        case FLUXION_RHI_FORMAT_R8G8B8A8_SRGB:
        case FLUXION_RHI_FORMAT_B8G8R8A8_UNORM:
        case FLUXION_RHI_FORMAT_B8G8R8A8_SRGB:
            *outType = GL_UNSIGNED_BYTE; *outComponents = 4; *outNormalized = GL_TRUE; return;
        default: *outType = GL_FLOAT; *outComponents = 4; *outNormalized = GL_FALSE; return;
    }
}

// --- Adapter -------------------------------------------------------------

static bool Fluxion_RHIOpenGL_IsPlaceholderAdapter(FluxionRHIAdapterHandle adapter)
{
    return adapter.index == 0 && adapter.generation == 0;
}

static void Fluxion_RHIOpenGL_QueryLimitsAndCapabilities(FluxionRHICapabilityFlags* outCaps, FluxionRHILimits* outLimits)
{
    // Compute/tessellation/geometry are core in GL
    // 4.5, so they're unconditionally reported; async compute/transfer,
    // timeline sync, bindless/descriptor-indexing, buffer-device-address,
    // ray tracing/query, mesh shaders, VRS, sparse resources, and
    // indirect-count are all left unset -- none of them have a real
    // OpenGL 4.5-core equivalent this backend implements.
    *outCaps = FLUXION_RHI_CAPABILITY_COMPUTE_SHADERS | FLUXION_RHI_CAPABILITY_TESSELLATION | FLUXION_RHI_CAPABILITY_GEOMETRY_SHADERS;

    std::memset(outLimits, 0, sizeof(*outLimits));
    GLint value = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value); outLimits->maxTextureDimension2D = (u32)value; outLimits->maxTextureDimension1D = (u32)value;
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &value); outLimits->maxTextureDimension3D = (u32)value;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &value); outLimits->maxTextureArrayLayers = (u32)value;
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &value); outLimits->maxColorAttachments = (u32)value;
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &value); outLimits->maxComputeWorkGroupInvocations = (u32)value;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &value); outLimits->minUniformBufferOffsetAlignment = (u32)value;
    for (u32 i = 0; i < 3; ++i)
    {
        GLint size = 0;
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, i, &size);
        outLimits->maxComputeWorkGroupSize[i] = (u32)size;
    }
    outLimits->maxBoundDescriptorSets = FLUXION_RHI_MAX_BIND_GROUPS;
    outLimits->maxPushConstantSize = 0; // no push-constant equivalent in OpenGL
}

static bool Fluxion_RHIOpenGL_GetAdapterInfo(FluxionRHIAdapterHandle adapter, FluxionRHIAdapterInfo* outInfo)
{
    if (!Fluxion_RHIOpenGL_IsPlaceholderAdapter(adapter) || outInfo == nullptr) return false;
    std::memset(outInfo, 0, sizeof(*outInfo));

    if (s_activeDevice != nullptr && s_activeDevice->contextValid)
    {
        const char* renderer = (const char*)glGetString(GL_RENDERER);
        const char* vendor = (const char*)glGetString(GL_VENDOR);
#if defined(_MSC_VER)
        strncpy_s(outInfo->name, sizeof(outInfo->name), renderer != nullptr ? renderer : "OpenGL Adapter", sizeof(outInfo->name) - 1);
#else
        std::strncpy(outInfo->name, renderer != nullptr ? renderer : "OpenGL Adapter", sizeof(outInfo->name) - 1);
#endif
        FLUXION_UNUSED(vendor);
        outInfo->type = FLUXION_RHI_ADAPTER_TYPE_OTHER; // OpenGL has no portable way to distinguish discrete/integrated
        outInfo->capabilities = s_activeDevice->capabilities;
        outInfo->limits = s_activeDevice->limits;
    }
    else
    {
        // No device/context exists yet -- report a name-only placeholder;
        // real capabilities/limits are only knowable once a real context
        // exists (Fluxion_RHI_CreateDevice), matching the design note at
        // the top of this file.
#if defined(_MSC_VER)
        strncpy_s(outInfo->name, sizeof(outInfo->name), "OpenGL Adapter (pending context)", sizeof(outInfo->name) - 1);
#else
        std::strncpy(outInfo->name, "OpenGL Adapter (pending context)", sizeof(outInfo->name) - 1);
#endif
        outInfo->type = FLUXION_RHI_ADAPTER_TYPE_OTHER;
        // No live context yet -- capabilities/limits stay zeroed rather
        // than risking a call through an unloaded GL function pointer;
        // a caller that needs real numbers should query again after
        // Fluxion_RHI_CreateDevice.
    }
    return true;
}

static u32 Fluxion_RHIOpenGL_EnumerateAdapters(FluxionRHIInstanceHandle instance, FluxionRHIAdapterHandle* outAdapters, u32 maxAdapters)
{
    FLUXION_UNUSED(instance);
    if (outAdapters == nullptr) return 1;
    if (maxAdapters == 0) return 0;
    outAdapters[0].index = 0;
    outAdapters[0].generation = 0;
    return 1;
}

// --- Device / queue --------------------------------------------------------

static FluxionRHIDeviceHandle Fluxion_RHIOpenGL_CreateDevice(FluxionRHIAdapterHandle adapter, const FluxionRHIDeviceDesc* desc)
{
    FluxionRHIDeviceHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!Fluxion_RHIOpenGL_IsPlaceholderAdapter(adapter)) return invalid;
    // Only one live GL context is supported by this backend (see the
    // design note at the top of this file) -- a second concurrent device
    // is rejected outright rather than silently aliasing the same context.
    if (s_activeDevice != nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIOpenGL_PoolAllocate(s_deviceSlots, FLUXION_RHI_OPENGL_MAX_DEVICES, &index, &generation)) return invalid;

    FluxionRHIOpenGLDevice* deviceState = &s_devices[index];
    *deviceState = FluxionRHIOpenGLDevice{};

    if (!Fluxion_RHIOpenGL_CreateContext(deviceState, s_wantValidation))
    {
        Fluxion_RHIOpenGL_PoolFree(s_deviceSlots, FLUXION_RHI_OPENGL_MAX_DEVICES, index, generation);
        return invalid;
    }

    Fluxion_RHIOpenGL_QueryLimitsAndCapabilities(&deviceState->capabilities, &deviceState->limits);

    if (desc != nullptr && (deviceState->capabilities & desc->requiredCapabilities) != desc->requiredCapabilities)
    {
        Fluxion_RHIOpenGL_DestroyContext(deviceState);
        Fluxion_RHIOpenGL_PoolFree(s_deviceSlots, FLUXION_RHI_OPENGL_MAX_DEVICES, index, generation);
        return invalid;
    }

    s_activeDevice = deviceState;

    FluxionRHIDeviceHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

static void Fluxion_RHIOpenGL_DestroyDevice(FluxionRHIDeviceHandle device)
{
    FluxionRHIOpenGLDevice* deviceState = Fluxion_RHIOpenGL_ResolveDevice(device);
    if (deviceState == nullptr) return;

    Fluxion_RHIOpenGL_DestroyContext(deviceState);
    if (s_activeDevice == deviceState) s_activeDevice = nullptr;
    Fluxion_RHIOpenGL_PoolFree(s_deviceSlots, FLUXION_RHI_OPENGL_MAX_DEVICES, device.index, device.generation);
}

// This backend never has anything genuinely GPU-timeline-deferred to
// reclaim: CommandList execution is immediate/
// synchronous, and Destroy* frees the underlying GL object right away in
// every OpenGL*.cpp file -- CollectGarbage is a no-op, same as the Null
// backend.
static void Fluxion_RHIOpenGL_CollectGarbage(FluxionRHIDeviceHandle device)
{
    FLUXION_UNUSED(device);
}

static FluxionRHIBackendType Fluxion_RHIOpenGL_GetDeviceBackendType(FluxionRHIDeviceHandle device)
{
    FLUXION_UNUSED(device);
    return FLUXION_RHI_BACKEND_OPENGL;
}

static FluxionRHIQueueHandle Fluxion_RHIOpenGL_GetQueue(FluxionRHIDeviceHandle device, FluxionRHIQueueType type)
{
    FluxionRHIQueueHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (Fluxion_RHIOpenGL_ResolveDevice(device) == nullptr) return invalid;
    // Graphics/Compute/Transfer all resolve to the
    // same underlying GL context -- the *3+type encoding is kept purely
    // so the handle still round-trips through the same "index / 3 = which
    // device" decoding every other backend uses, even though every queue
    // type behaves identically here.
    FluxionRHIQueueHandle queue;
    queue.index = device.index * 3u + (u32)type;
    queue.generation = device.generation;
    return queue;
}

// --- Native handle escape hatch ---------------------------------------------

FluxionRHINativeHandle Fluxion_RHIOpenGL_GetNativeDeviceHandle(FluxionRHIDeviceHandle device)
{
    FluxionRHIOpenGLDevice* deviceState = Fluxion_RHIOpenGL_ResolveDevice(device);
#if defined(_WIN32)
    FluxionRHINativeHandle handle = { deviceState != nullptr ? (void*)deviceState->context : nullptr };
#else
    FluxionRHINativeHandle handle = { deviceState != nullptr ? (void*)deviceState->context : nullptr };
#endif
    return handle;
}

FluxionRHINativeHandle Fluxion_RHIOpenGL_GetNativeBufferHandle(FluxionRHIBufferHandle buffer)
{
    FluxionRHIOpenGLBuffer* bufferState = Fluxion_RHIOpenGL_ResolveBuffer(buffer);
    FluxionRHINativeHandle handle = { bufferState != nullptr ? (void*)(uintptr_t)bufferState->name : nullptr };
    return handle;
}

FluxionRHINativeHandle Fluxion_RHIOpenGL_GetNativeTextureHandle(FluxionRHITextureHandle texture)
{
    FluxionRHIOpenGLTexture* textureState = Fluxion_RHIOpenGL_ResolveTexture(texture);
    FluxionRHINativeHandle handle = { textureState != nullptr ? (void*)(uintptr_t)textureState->name : nullptr };
    return handle;
}

// --- Instance lifetime / vtable / entry point -------------------------------

static void Fluxion_RHIOpenGL_DestroyInstance(FluxionRHIInstanceHandle instance)
{
    FLUXION_UNUSED(instance);
    for (u32 i = 0; i < FLUXION_RHI_OPENGL_MAX_DEVICES; ++i)
    {
        if (s_deviceSlots[i].alive)
        {
            FluxionRHIDeviceHandle handle = { i, s_deviceSlots[i].generation };
            Fluxion_RHIOpenGL_DestroyDevice(handle);
        }
    }
}

static const FluxionRHIBackendVTable s_openglVTable = {
    Fluxion_RHIOpenGL_DestroyInstance,
    Fluxion_RHIOpenGL_EnumerateAdapters,
    Fluxion_RHIOpenGL_GetAdapterInfo,

    Fluxion_RHIOpenGL_CreateDevice,
    Fluxion_RHIOpenGL_DestroyDevice,
    Fluxion_RHIOpenGL_CollectGarbage,
    Fluxion_RHIOpenGL_GetDeviceBackendType,
    Fluxion_RHIOpenGL_GetQueue,

    Fluxion_RHIOpenGL_CreateCommandList,
    Fluxion_RHIOpenGL_DestroyCommandList,
    Fluxion_RHIOpenGL_CommandListBegin,
    Fluxion_RHIOpenGL_CommandListEnd,
    Fluxion_RHIOpenGL_CommandListBeginRendering,
    Fluxion_RHIOpenGL_CommandListEndRendering,
    Fluxion_RHIOpenGL_CommandListSetPipeline,
    Fluxion_RHIOpenGL_CommandListSetVertexBuffer,
    Fluxion_RHIOpenGL_CommandListSetIndexBuffer,
    Fluxion_RHIOpenGL_CommandListDraw,
    Fluxion_RHIOpenGL_CommandListDrawIndexed,
    Fluxion_RHIOpenGL_CommandListDrawIndirect,
    Fluxion_RHIOpenGL_CommandListDispatch,
    Fluxion_RHIOpenGL_CommandListCopyBuffer,
    Fluxion_RHIOpenGL_CommandListCopyTexture,
    Fluxion_RHIOpenGL_CommandListCopyBufferToTexture,
    Fluxion_RHIOpenGL_CommandListBarrier,
    Fluxion_RHIOpenGL_CommandListSetBindGroup,

    Fluxion_RHIOpenGL_QueueSubmit,

    Fluxion_RHIOpenGL_CreateBuffer,
    Fluxion_RHIOpenGL_DestroyBuffer,
    Fluxion_RHIOpenGL_MapBuffer,
    Fluxion_RHIOpenGL_UnmapBuffer,
    Fluxion_RHIOpenGL_CreateTexture,
    Fluxion_RHIOpenGL_DestroyTexture,
    Fluxion_RHIOpenGL_CreateTextureView,
    Fluxion_RHIOpenGL_DestroyTextureView,
    Fluxion_RHIOpenGL_CreateSampler,
    Fluxion_RHIOpenGL_DestroySampler,

    Fluxion_RHIOpenGL_CreateBindGroupLayout,
    Fluxion_RHIOpenGL_DestroyBindGroupLayout,
    Fluxion_RHIOpenGL_CreateBindGroup,
    Fluxion_RHIOpenGL_DestroyBindGroup,

    Fluxion_RHIOpenGL_CreateShader,
    Fluxion_RHIOpenGL_DestroyShader,
    Fluxion_RHIOpenGL_CreateGraphicsPipeline,
    Fluxion_RHIOpenGL_CreateComputePipeline,
    Fluxion_RHIOpenGL_DestroyPipeline,
    Fluxion_RHIOpenGL_SavePipelineCacheToFile,
    Fluxion_RHIOpenGL_LoadPipelineCacheFromFile,

    Fluxion_RHIOpenGL_CreateSwapchain,
    Fluxion_RHIOpenGL_DestroySwapchain,
    Fluxion_RHIOpenGL_SwapchainAcquireNextImage,
    Fluxion_RHIOpenGL_SwapchainGetTexture,
    Fluxion_RHIOpenGL_SwapchainPresent,
    Fluxion_RHIOpenGL_SwapchainGetExtent,

    Fluxion_RHIOpenGL_CreateFence,
    Fluxion_RHIOpenGL_DestroyFence,
    Fluxion_RHIOpenGL_WaitForFence,
    Fluxion_RHIOpenGL_ResetFence,
    Fluxion_RHIOpenGL_CreateSemaphore,
    Fluxion_RHIOpenGL_DestroySemaphore,
    Fluxion_RHIOpenGL_CreateQueryPool,
    Fluxion_RHIOpenGL_DestroyQueryPool,
    Fluxion_RHIOpenGL_CommandListResetQueryPool,
    Fluxion_RHIOpenGL_CommandListWriteTimestamp,
    Fluxion_RHIOpenGL_QueryPoolGetResults,
    Fluxion_RHIOpenGL_GetTimestampFrequency,

    Fluxion_RHIOpenGL_GetNativeDeviceHandle,
    Fluxion_RHIOpenGL_GetNativeBufferHandle,
    Fluxion_RHIOpenGL_GetNativeTextureHandle,
};

extern "C" const FluxionRHIBackendVTable* Fluxion_RHI_OpenGL_CreateInstance(const FluxionRHIInstanceDesc* desc, FluxionRHIInstanceHandle* outInstance)
{
    s_wantValidation = desc != nullptr && desc->enableValidation;
    for (u32 i = 0; i < FLUXION_RHI_OPENGL_MAX_DEVICES; ++i) s_deviceSlots[i] = {};
    s_activeDevice = nullptr;

    outInstance->index = 0;
    outInstance->generation = 0;
    return &s_openglVTable;
}
