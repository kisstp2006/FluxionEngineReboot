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

// OpenGL RHI backend entry point: instance/adapter enumeration, device
// (= real WGL/GLX context) creation, format mapping, and the dispatching
// vtable. See OpenGLCommon.h for the hidden bootstrap window. One
// adapter only ("the current OpenGL implementation"): real adapter info
// needs a context, so EnumerateAdapters hands out a placeholder and
// GetAdapterInfo lazily creates the bootstrap context on first ask,
// keeping it for reuse as the device context.

#include "OpenGLCommon.h"
#include "OpenGLFunctions.h"

#include <Fluxion/Foundation/Log.h>

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

        // The BC formats reach desktop GL under their own names: BC4/BC5
        // as RGTC, BC6H/BC7 as BPTC. Both are core since 3.0 and 4.2
        // respectively, so no extension check guards them here.
        case FLUXION_RHI_FORMAT_BC4_UNORM: return GL_COMPRESSED_RED_RGTC1;
        case FLUXION_RHI_FORMAT_BC5_UNORM: return GL_COMPRESSED_RG_RGTC2;
        case FLUXION_RHI_FORMAT_BC6H_UFLOAT: return GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT;
        case FLUXION_RHI_FORMAT_BC7_UNORM: return GL_COMPRESSED_RGBA_BPTC_UNORM;
        case FLUXION_RHI_FORMAT_BC7_SRGB: return GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM;

        // ASTC's high-dynamic-range blocks are read through the SAME
        // tokens as its ordinary ones -- the HDR extension adds a decoder,
        // not a format. So the linear and the floating-point entries
        // deliberately land on one token here, and it is the sRGB one that
        // differs.
        case FLUXION_RHI_FORMAT_ASTC_4X4_UNORM:
        case FLUXION_RHI_FORMAT_ASTC_4X4_FLOAT: return GL_COMPRESSED_RGBA_ASTC_4x4_KHR;
        case FLUXION_RHI_FORMAT_ASTC_4X4_SRGB: return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR;
        case FLUXION_RHI_FORMAT_ASTC_6X6_UNORM:
        case FLUXION_RHI_FORMAT_ASTC_6X6_FLOAT: return GL_COMPRESSED_RGBA_ASTC_6x6_KHR;
        case FLUXION_RHI_FORMAT_ASTC_6X6_SRGB: return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR;
        case FLUXION_RHI_FORMAT_ASTC_8X8_UNORM:
        case FLUXION_RHI_FORMAT_ASTC_8X8_FLOAT: return GL_COMPRESSED_RGBA_ASTC_8x8_KHR;
        case FLUXION_RHI_FORMAT_ASTC_8X8_SRGB: return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR;

        default: return GL_RGBA8;
    }
}

bool Fluxion_RHIOpenGL_DeviceIsFormatSupported(FluxionRHIDeviceHandle device, FluxionRHIFormat format)
{
    if (Fluxion_RHIOpenGL_ResolveDevice(device) == nullptr) return false;
    if (Fluxion_RHI_GetFormatInfo(format).blockBytes == 0) return false;

    // The mapper answers GL_RGBA8 for anything it does not know, so a
    // format that lands there without being asked for is one this backend
    // cannot hold -- and saying yes would mean a texture quietly created
    // in the wrong format rather than not created at all.
    const GLenum internalFormat = Fluxion_RHIOpenGL_MapSizedInternalFormat(format);
    if (internalFormat == GL_RGBA8 && format != FLUXION_RHI_FORMAT_R8G8B8A8_UNORM && format != FLUXION_RHI_FORMAT_B8G8R8A8_UNORM)
    {
        return false;
    }

    // Whether the driver will actually allocate it, asked of the driver.
    GLint supported = GL_FALSE;
    glGetInternalformativ(GL_TEXTURE_2D, internalFormat, GL_INTERNALFORMAT_SUPPORTED, 1, &supported);
    return supported == GL_TRUE;
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
        // Half, and it has to be said. This pair decides how wide the
        // driver believes each texel in the SOURCE bytes is, and a half
        // texture described as full floats is read at twice its stride:
        // every row picks up the row after it, and the last one runs off
        // the end of the buffer -- which is the only part of that a
        // driver can see, and it is the last thing to go wrong rather
        // than the first.
        case FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT:
            *outFormat = GL_RGBA; *outType = GL_HALF_FLOAT; return;
        case FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT:
            *outFormat = GL_RGBA; *outType = GL_FLOAT; return;
        case FLUXION_RHI_FORMAT_R32G32B32_FLOAT:
            *outFormat = GL_RGB; *outType = GL_FLOAT; return;
        case FLUXION_RHI_FORMAT_R32G32_FLOAT:
            *outFormat = GL_RG; *outType = GL_FLOAT; return;

        // Depth is its own channel here, not a red one. Reading a shadow
        // map back is what needs this, and asking for GL_RED of a depth
        // texture is refused -- which leaves the destination untouched,
        // so what comes out is whatever was in it rather than an error
        // anybody sees.
        case FLUXION_RHI_FORMAT_D32_FLOAT:
            *outFormat = GL_DEPTH_COMPONENT; *outType = GL_FLOAT; return;
        case FLUXION_RHI_FORMAT_D24_UNORM_S8_UINT:
            *outFormat = GL_DEPTH_STENCIL; *outType = GL_UNSIGNED_INT_24_8; return;

        default:
            // Said out loud rather than guessed at. Everything above is a
            // format this backend can move pixels for; anything else
            // reaching here is a format somebody added without coming
            // through this function, and answering with a plausible pair
            // would upload it at the wrong stride and report nothing.
            FLUXION_LOG_ERROR("RHI.OpenGL", "no pixel transfer format for %d; the upload would be read at the wrong stride", (int)format);
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
    Fluxion_RHIOpenGL_CommandListSetViewport,
    Fluxion_RHIOpenGL_CommandListSetScissor,
    Fluxion_RHIOpenGL_CommandListSetPipeline,
    Fluxion_RHIOpenGL_CommandListSetVertexBuffer,
    Fluxion_RHIOpenGL_CommandListSetIndexBuffer,
    Fluxion_RHIOpenGL_CommandListDraw,
    Fluxion_RHIOpenGL_CommandListDrawIndexed,
    Fluxion_RHIOpenGL_CommandListDrawIndirect,
    Fluxion_RHIOpenGL_CommandListDrawIndexedIndirect,
    Fluxion_RHIOpenGL_CommandListDispatch,
    Fluxion_RHIOpenGL_CommandListCopyBuffer,
    Fluxion_RHIOpenGL_CommandListCopyTexture,
    Fluxion_RHIOpenGL_CommandListCopyBufferToTexture,
    Fluxion_RHIOpenGL_CommandListCopyTextureToBuffer,
    Fluxion_RHIOpenGL_DeviceIsFormatSupported,
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
