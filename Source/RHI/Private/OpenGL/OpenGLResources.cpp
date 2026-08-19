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

// Buffer / Texture / TextureView / Sampler. The storage-flag and
// persistent-mapping choices are explained at their use sites below.

#include "OpenGLCommon.h"

#include <Fluxion/Foundation/Log.h>
#include "OpenGLFunctions.h"

// --- Buffers -------------------------------------------------------------

static FluxionRHIOpenGLSlot s_bufferSlots[FLUXION_RHI_OPENGL_MAX_BUFFERS];
static FluxionRHIOpenGLBuffer s_buffers[FLUXION_RHI_OPENGL_MAX_BUFFERS];

FluxionRHIOpenGLBuffer* Fluxion_RHIOpenGL_ResolveBuffer(FluxionRHIBufferHandle buffer)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_bufferSlots, FLUXION_RHI_OPENGL_MAX_BUFFERS, buffer.index, buffer.generation)) return nullptr;
    return &s_buffers[buffer.index];
}

FluxionRHIBufferHandle Fluxion_RHIOpenGL_CreateBuffer(FluxionRHIDeviceHandle device, const FluxionRHIBufferDesc* desc)
{
    FluxionRHIBufferHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIOpenGLDevice* deviceState = Fluxion_RHIOpenGL_ResolveDevice(device);
    if (deviceState == nullptr || desc == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIOpenGL_PoolAllocate(s_bufferSlots, FLUXION_RHI_OPENGL_MAX_BUFFERS, &index, &generation)) return invalid;

    FluxionRHIOpenGLBuffer* bufferState = &s_buffers[index];
    *bufferState = FluxionRHIOpenGLBuffer{};
    bufferState->size = desc->size > 0 ? desc->size : 1;

    GLbitfield storageFlags = 0;
    switch (desc->memoryClass)
    {
        case FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU:
            storageFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | GL_DYNAMIC_STORAGE_BIT;
            bufferState->cpuVisible = true;
            break;
        case FLUXION_RHI_MEMORY_CLASS_GPU_TO_CPU:
        case FLUXION_RHI_MEMORY_CLASS_READBACK:
            storageFlags = GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            bufferState->cpuVisible = true;
            break;
        case FLUXION_RHI_MEMORY_CLASS_GPU_ONLY:
        case FLUXION_RHI_MEMORY_CLASS_TRANSIENT:
        default:
            // No flags at all: storage the device owns outright. In
            // particular no GL_DYNAMIC_STORAGE_BIT -- everything reaches
            // this class through server-side copies, and a driver told
            // the client might write keeps the buffer in host memory,
            // read across the bus on every draw.
            //
            // A driver may still emit a one-time "moved from VIDEO to
            // HOST" note about small buffers of this class. That was
            // probed from every side this backend controls and none of it
            // changes the decision; measured frame time matches the other
            // backends. Left visible rather than filtered -- a rule that
            // hides one vendor's noise hides another's real complaint.
            storageFlags = 0;
            bufferState->cpuVisible = false;
            break;
    }

    glCreateBuffers(1, &bufferState->name);
    glNamedBufferStorage(bufferState->name, (GLsizeiptr)bufferState->size, nullptr, storageFlags);

    if (bufferState->cpuVisible)
    {
        GLbitfield mapFlags = (storageFlags & (GL_MAP_WRITE_BIT | GL_MAP_READ_BIT)) | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        bufferState->mappedPointer = glMapNamedBufferRange(bufferState->name, 0, (GLsizeiptr)bufferState->size, mapFlags);
    }

    Fluxion_RHIOpenGL_LabelObject(GL_BUFFER, bufferState->name, desc->debugName);

    FluxionRHIBufferHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIOpenGL_DestroyBuffer(FluxionRHIBufferHandle buffer)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_bufferSlots, FLUXION_RHI_OPENGL_MAX_BUFFERS, buffer.index, buffer.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI OpenGL backend: destroy called with an invalid or already-destroyed buffer handle");
        return;
    }
    FluxionRHIOpenGLBuffer* bufferState = &s_buffers[buffer.index];
    if (bufferState->mappedPointer != nullptr)
    {
        glUnmapNamedBuffer(bufferState->name);
    }
    if (bufferState->name != 0)
    {
        Fluxion_RHIOpenGL_BindingCacheForgetBuffer(bufferState->name);
        glDeleteBuffers(1, &bufferState->name);
    }
    *bufferState = FluxionRHIOpenGLBuffer{};
    Fluxion_RHIOpenGL_PoolFree(s_bufferSlots, FLUXION_RHI_OPENGL_MAX_BUFFERS, buffer.index, buffer.generation);
}

void Fluxion_RHIOpenGL_LabelObject(GLenum identifier, GLuint name, const char* label)
{
    if (label == nullptr || label[0] == '\0' || name == 0 || glObjectLabel == nullptr) return;
    // -1: the label is null-terminated, so GL measures it itself.
    glObjectLabel(identifier, name, -1, label);
}

// A CPU-visible buffer is mapped persistently at creation time
// -- Map just hands back the pointer already returned by
// glMapNamedBufferRange, and Unmap (below) is a no-op for the buffer's
// lifetime; a GPU_ONLY/TRANSIENT buffer returns NULL, matching the
// documented contract.
void* Fluxion_RHIOpenGL_MapBuffer(FluxionRHIBufferHandle buffer)
{
    FluxionRHIOpenGLBuffer* bufferState = Fluxion_RHIOpenGL_ResolveBuffer(buffer);
    if (bufferState == nullptr) return nullptr;
    return bufferState->mappedPointer;
}

void Fluxion_RHIOpenGL_UnmapBuffer(FluxionRHIBufferHandle buffer)
{
    FLUXION_UNUSED(buffer);
    // Intentional no-op -- see the persistent-mapping comment above.
}

// --- Textures --------------------------------------------------------------

static FluxionRHIOpenGLSlot s_textureSlots[FLUXION_RHI_OPENGL_MAX_TEXTURES];
static FluxionRHIOpenGLTexture s_textures[FLUXION_RHI_OPENGL_MAX_TEXTURES];

FluxionRHIOpenGLTexture* Fluxion_RHIOpenGL_ResolveTexture(FluxionRHITextureHandle texture)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_textureSlots, FLUXION_RHI_OPENGL_MAX_TEXTURES, texture.index, texture.generation)) return nullptr;
    return &s_textures[texture.index];
}

static GLenum Fluxion_RHIOpenGL_PickTextureTarget(const FluxionRHITextureDesc* desc)
{
    // Asked before the layer count, because six layers alone do not make
    // a cube: six layers of a wall atlas are six layers.
    if (desc->dimension == FLUXION_RHI_TEXTURE_DIMENSION_CUBE) return GL_TEXTURE_CUBE_MAP;
    if (desc->arrayLayers > 1) return GL_TEXTURE_2D_ARRAY;
    if (desc->depth > 1) return GL_TEXTURE_3D;
    return GL_TEXTURE_2D;
}

FluxionRHITextureHandle Fluxion_RHIOpenGL_CreateTexture(FluxionRHIDeviceHandle device, const FluxionRHITextureDesc* desc)
{
    FluxionRHITextureHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIOpenGLDevice* deviceState = Fluxion_RHIOpenGL_ResolveDevice(device);
    if (deviceState == nullptr || desc == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIOpenGL_PoolAllocate(s_textureSlots, FLUXION_RHI_OPENGL_MAX_TEXTURES, &index, &generation)) return invalid;

    FluxionRHIOpenGLTexture* textureState = &s_textures[index];
    *textureState = FluxionRHIOpenGLTexture{};
    textureState->target = Fluxion_RHIOpenGL_PickTextureTarget(desc);
    textureState->width = desc->width;
    textureState->height = desc->height;
    textureState->depth = desc->depth > 0 ? desc->depth : 1;
    textureState->mipLevels = desc->mipLevels > 0 ? desc->mipLevels : 1;
    textureState->arrayLayers = desc->arrayLayers > 0 ? desc->arrayLayers : 1;
    textureState->format = desc->format;

    if (desc->dimension == FLUXION_RHI_TEXTURE_DIMENSION_CUBE &&
        (textureState->arrayLayers != FLUXION_RHI_CUBE_FACE_COUNT || desc->width != desc->height))
    {
        FLUXION_LOG_ERROR("RHI.OpenGL", "a cube texture needs six square layers; this one has %u layers at %ux%u",
            textureState->arrayLayers, desc->width, desc->height);
        Fluxion_RHIOpenGL_PoolFree(s_textureSlots, FLUXION_RHI_OPENGL_MAX_TEXTURES, index, generation);
        return invalid;
    }

    GLenum internalFormat = Fluxion_RHIOpenGL_MapSizedInternalFormat(desc->format);
    glCreateTextures(textureState->target, 1, &textureState->name);
    if (textureState->target == GL_TEXTURE_CUBE_MAP)
    {
        // Two dimensions, not three: a cube map's storage call takes one
        // face's size and allocates all six. Handing it the layer count
        // as a depth is the mistake this branch exists to avoid.
        glTextureStorage2D(textureState->name, (GLsizei)textureState->mipLevels, internalFormat,
            (GLsizei)textureState->width, (GLsizei)textureState->height);
    }
    else if (textureState->target == GL_TEXTURE_2D)
    {
        glTextureStorage2D(textureState->name, (GLsizei)textureState->mipLevels, internalFormat, (GLsizei)textureState->width, (GLsizei)textureState->height);
    }
    else if (textureState->target == GL_TEXTURE_2D_ARRAY)
    {
        glTextureStorage3D(textureState->name, (GLsizei)textureState->mipLevels, internalFormat, (GLsizei)textureState->width, (GLsizei)textureState->height, (GLsizei)textureState->arrayLayers);
    }
    else // GL_TEXTURE_3D
    {
        glTextureStorage3D(textureState->name, (GLsizei)textureState->mipLevels, internalFormat, (GLsizei)textureState->width, (GLsizei)textureState->height, (GLsizei)textureState->depth);
    }

    Fluxion_RHIOpenGL_LabelObject(GL_TEXTURE, textureState->name, desc->debugName);

    FluxionRHITextureHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIOpenGL_DestroyTexture(FluxionRHITextureHandle texture)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_textureSlots, FLUXION_RHI_OPENGL_MAX_TEXTURES, texture.index, texture.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI OpenGL backend: destroy called with an invalid or already-destroyed texture handle");
        return;
    }
    FluxionRHIOpenGLTexture* textureState = &s_textures[texture.index];
    if (textureState->name != 0 && !textureState->isSwapchainImage)
    {
        Fluxion_RHIOpenGL_BindingCacheForgetTexture(textureState->name);
        glDeleteTextures(1, &textureState->name);
    }
    *textureState = FluxionRHIOpenGLTexture{};
    Fluxion_RHIOpenGL_PoolFree(s_textureSlots, FLUXION_RHI_OPENGL_MAX_TEXTURES, texture.index, texture.generation);
}

// One level, no array: the window's picture, as ordinary a texture as any
// other so that every path that renders into one works on it unchanged.
static void Fluxion_RHIOpenGL_AllocateSwapchainStorage(FluxionRHIOpenGLTexture* textureState, u32 width, u32 height)
{
    textureState->target = GL_TEXTURE_2D;
    textureState->width = width;
    textureState->height = height;
    textureState->depth = 1;
    textureState->mipLevels = 1;
    textureState->arrayLayers = 1;

    glCreateTextures(GL_TEXTURE_2D, 1, &textureState->name);
    glTextureStorage2D(textureState->name, 1, Fluxion_RHIOpenGL_MapSizedInternalFormat(textureState->format), (GLsizei)width, (GLsizei)height);
    Fluxion_RHIOpenGL_LabelObject(GL_TEXTURE, textureState->name, "Swapchain.Image");
}

bool Fluxion_RHIOpenGL_CreateSwapchainTexture(u32 width, u32 height, FluxionRHIFormat format, FluxionRHITextureHandle* outHandle)
{
    if (outHandle == nullptr || width == 0 || height == 0) return false;

    u32 index, generation;
    if (!Fluxion_RHIOpenGL_PoolAllocate(s_textureSlots, FLUXION_RHI_OPENGL_MAX_TEXTURES, &index, &generation)) return false;

    s_textures[index] = FluxionRHIOpenGLTexture{};
    s_textures[index].format = format;
    s_textures[index].isSwapchainImage = true;
    Fluxion_RHIOpenGL_AllocateSwapchainStorage(&s_textures[index], width, height);

    outHandle->index = index;
    outHandle->generation = generation;
    return true;
}

bool Fluxion_RHIOpenGL_ResizeSwapchainTexture(FluxionRHITextureHandle handle, u32 width, u32 height)
{
    FluxionRHIOpenGLTexture* textureState = Fluxion_RHIOpenGL_ResolveTexture(handle);
    if (textureState == nullptr || !textureState->isSwapchainImage || width == 0 || height == 0) return false;
    if (textureState->width == width && textureState->height == height) return true;

    // Storage allocated once cannot be resized, so the name goes with it
    // -- and the cache is told, because GL hands freed names straight back
    // out and a cache that still holds one skips the next bind of it.
    if (textureState->name != 0)
    {
        Fluxion_RHIOpenGL_BindingCacheForgetTexture(textureState->name);
        glDeleteTextures(1, &textureState->name);
        textureState->name = 0;
    }

    Fluxion_RHIOpenGL_AllocateSwapchainStorage(textureState, width, height);
    return true;
}

void Fluxion_RHIOpenGL_DestroySwapchainTexture(FluxionRHITextureHandle handle)
{
    FluxionRHIOpenGLTexture* textureState = Fluxion_RHIOpenGL_ResolveTexture(handle);
    if (textureState != nullptr && textureState->name != 0)
    {
        Fluxion_RHIOpenGL_BindingCacheForgetTexture(textureState->name);
        glDeleteTextures(1, &textureState->name);
    }
    if (textureState != nullptr) *textureState = FluxionRHIOpenGLTexture{};
    Fluxion_RHIOpenGL_PoolFree(s_textureSlots, FLUXION_RHI_OPENGL_MAX_TEXTURES, handle.index, handle.generation);
}

// --- Texture views -----------------------------------------------------------

static FluxionRHIOpenGLSlot s_textureViewSlots[FLUXION_RHI_OPENGL_MAX_TEXTURE_VIEWS];
static FluxionRHIOpenGLTextureView s_textureViews[FLUXION_RHI_OPENGL_MAX_TEXTURE_VIEWS];

FluxionRHIOpenGLTextureView* Fluxion_RHIOpenGL_ResolveTextureView(FluxionRHITextureViewHandle view)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_textureViewSlots, FLUXION_RHI_OPENGL_MAX_TEXTURE_VIEWS, view.index, view.generation)) return nullptr;
    return &s_textureViews[view.index];
}

FluxionRHITextureViewHandle Fluxion_RHIOpenGL_CreateTextureView(FluxionRHIDeviceHandle device, const FluxionRHITextureViewDesc* desc)
{
    FluxionRHITextureViewHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIOpenGLDevice* deviceState = Fluxion_RHIOpenGL_ResolveDevice(device);
    if (deviceState == nullptr || desc == nullptr) return invalid;

    FluxionRHIOpenGLTexture* textureState = Fluxion_RHIOpenGL_ResolveTexture(desc->texture);
    if (textureState == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIOpenGL_PoolAllocate(s_textureViewSlots, FLUXION_RHI_OPENGL_MAX_TEXTURE_VIEWS, &index, &generation)) return invalid;

    FluxionRHIOpenGLTextureView* viewState = &s_textureViews[index];
    *viewState = FluxionRHIOpenGLTextureView{};
    viewState->texture = desc->texture;

    {
        // A whole-resource, same-format view is just the owning texture's
        // own name -- glTextureView is only used for the general case
        // (a mip/layer sub-range or a format reinterpretation) to avoid
        // creating and tracking an extra GL object for the common case.
        bool wholeResource = desc->baseMipLevel == 0 && desc->baseArrayLayer == 0 &&
            (desc->mipLevelCount == 0 || desc->mipLevelCount >= textureState->mipLevels) &&
            (desc->arrayLayerCount == 0 || desc->arrayLayerCount >= textureState->arrayLayers) &&
            (desc->format == FLUXION_RHI_FORMAT_UNKNOWN || desc->format == textureState->format);

        if (wholeResource)
        {
            viewState->name = textureState->name;
            viewState->target = textureState->target;
            viewState->ownsName = false;
        }
        else
        {
            GLuint viewName = 0;

            // GENERATED, NOT CREATED. The newer call gives the name its
            // target there and then, and a name that already has one is
            // exactly what glTextureView refuses -- measured, as an
            // invalid-operation the first time anything asked for a view
            // of a single mip level.
            glGenTextures(1, &viewName);
            GLenum internalFormat = Fluxion_RHIOpenGL_MapSizedInternalFormat(desc->format != FLUXION_RHI_FORMAT_UNKNOWN ? desc->format : textureState->format);
            u32 mipCount = desc->mipLevelCount > 0 ? desc->mipLevelCount : (textureState->mipLevels - desc->baseMipLevel);
            u32 layerCount = desc->arrayLayerCount > 0 ? desc->arrayLayerCount : (textureState->arrayLayers - desc->baseArrayLayer);
            glTextureView(viewName, textureState->target, textureState->name, internalFormat, desc->baseMipLevel, mipCount, desc->baseArrayLayer, layerCount);
            viewState->name = viewName;
            viewState->target = textureState->target;
            viewState->ownsName = true;
        }
    }

    FluxionRHITextureViewHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

// Which texture this view is of -- see Fluxion_RHI_GetTextureViewTexture.
FluxionRHITextureHandle Fluxion_RHIOpenGL_GetTextureViewTexture(FluxionRHITextureViewHandle view)
{
    FluxionRHITextureHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHIOpenGLTextureView* viewState = Fluxion_RHIOpenGL_ResolveTextureView(view);
    return viewState != nullptr ? viewState->texture : invalid;
}

void Fluxion_RHIOpenGL_DestroyTextureView(FluxionRHITextureViewHandle view)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_textureViewSlots, FLUXION_RHI_OPENGL_MAX_TEXTURE_VIEWS, view.index, view.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI OpenGL backend: destroy called with an invalid or already-destroyed texture view handle");
        return;
    }
    FluxionRHIOpenGLTextureView* viewState = &s_textureViews[view.index];
    if (viewState->ownsName && viewState->name != 0)
    {
        Fluxion_RHIOpenGL_BindingCacheForgetTexture(viewState->name);
        glDeleteTextures(1, &viewState->name);
    }
    *viewState = FluxionRHIOpenGLTextureView{};
    Fluxion_RHIOpenGL_PoolFree(s_textureViewSlots, FLUXION_RHI_OPENGL_MAX_TEXTURE_VIEWS, view.index, view.generation);
}

// --- Samplers ----------------------------------------------------------------

static FluxionRHIOpenGLSlot s_samplerSlots[FLUXION_RHI_OPENGL_MAX_SAMPLERS];
static FluxionRHIOpenGLSampler s_samplers[FLUXION_RHI_OPENGL_MAX_SAMPLERS];

FluxionRHIOpenGLSampler* Fluxion_RHIOpenGL_ResolveSampler(FluxionRHISamplerHandle sampler)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_samplerSlots, FLUXION_RHI_OPENGL_MAX_SAMPLERS, sampler.index, sampler.generation)) return nullptr;
    return &s_samplers[sampler.index];
}

static GLint Fluxion_RHIOpenGL_MapFilter(FluxionRHIFilter minFilter, FluxionRHIFilter mipFilter, bool isMin)
{
    if (isMin)
    {
        if (minFilter == FLUXION_RHI_FILTER_LINEAR) return mipFilter == FLUXION_RHI_FILTER_LINEAR ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST;
        return mipFilter == FLUXION_RHI_FILTER_LINEAR ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
    }
    return minFilter == FLUXION_RHI_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST;
}

static GLint Fluxion_RHIOpenGL_MapAddressMode(FluxionRHIAddressMode mode)
{
    switch (mode)
    {
        case FLUXION_RHI_ADDRESS_MODE_MIRRORED_REPEAT: return GL_MIRRORED_REPEAT;
        case FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE: return GL_CLAMP_TO_EDGE;
        case FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_BORDER: return GL_CLAMP_TO_BORDER;
        case FLUXION_RHI_ADDRESS_MODE_REPEAT:
        default: return GL_REPEAT;
    }
}

// The same mapping the depth state uses, written out again here rather
// than shared: the pipeline's copy lives in another translation unit,
// and a shared helper would be one more header for eight cases.
static GLenum Fluxion_RHIOpenGL_MapSamplerCompareOp(FluxionRHICompareOp op)
{
    switch (op)
    {
        case FLUXION_RHI_COMPARE_OP_NEVER: return GL_NEVER;
        case FLUXION_RHI_COMPARE_OP_EQUAL: return GL_EQUAL;
        case FLUXION_RHI_COMPARE_OP_LESS_OR_EQUAL: return GL_LEQUAL;
        case FLUXION_RHI_COMPARE_OP_GREATER: return GL_GREATER;
        case FLUXION_RHI_COMPARE_OP_NOT_EQUAL: return GL_NOTEQUAL;
        case FLUXION_RHI_COMPARE_OP_GREATER_OR_EQUAL: return GL_GEQUAL;
        case FLUXION_RHI_COMPARE_OP_ALWAYS: return GL_ALWAYS;
        default: return GL_LESS;
    }
}

FluxionRHISamplerHandle Fluxion_RHIOpenGL_CreateSampler(FluxionRHIDeviceHandle device, const FluxionRHISamplerDesc* desc)
{
    FluxionRHISamplerHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIOpenGLDevice* deviceState = Fluxion_RHIOpenGL_ResolveDevice(device);
    if (deviceState == nullptr || desc == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIOpenGL_PoolAllocate(s_samplerSlots, FLUXION_RHI_OPENGL_MAX_SAMPLERS, &index, &generation)) return invalid;

    FluxionRHIOpenGLSampler* samplerState = &s_samplers[index];
    *samplerState = FluxionRHIOpenGLSampler{};
    glCreateSamplers(1, &samplerState->name);
    glSamplerParameteri(samplerState->name, GL_TEXTURE_MIN_FILTER, Fluxion_RHIOpenGL_MapFilter(desc->minFilter, desc->mipFilter, true));
    glSamplerParameteri(samplerState->name, GL_TEXTURE_MAG_FILTER, Fluxion_RHIOpenGL_MapFilter(desc->magFilter, desc->mipFilter, false));
    glSamplerParameteri(samplerState->name, GL_TEXTURE_WRAP_S, Fluxion_RHIOpenGL_MapAddressMode(desc->addressModeU));
    glSamplerParameteri(samplerState->name, GL_TEXTURE_WRAP_T, Fluxion_RHIOpenGL_MapAddressMode(desc->addressModeV));
    glSamplerParameteri(samplerState->name, GL_TEXTURE_WRAP_R, Fluxion_RHIOpenGL_MapAddressMode(desc->addressModeW));
    if (desc->maxAnisotropy > 1.0f && deviceState->hasAnisotropicFiltering)
    {
        glSamplerParameterf(samplerState->name, GL_TEXTURE_MAX_ANISOTROPY, desc->maxAnisotropy);
    }

    // A comparing sampler returns how much of the filter kernel passed
    // the test, not the depths themselves -- switched on here, and left
    // alone otherwise so an ordinary sampler is exactly what it was.
    if (desc->compareEnable)
    {
        glSamplerParameteri(samplerState->name, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(samplerState->name, GL_TEXTURE_COMPARE_FUNC, Fluxion_RHIOpenGL_MapSamplerCompareOp(desc->compareOp));
    }

    Fluxion_RHIOpenGL_LabelObject(GL_SAMPLER, samplerState->name, desc->debugName);

    FluxionRHISamplerHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIOpenGL_DestroySampler(FluxionRHISamplerHandle sampler)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_samplerSlots, FLUXION_RHI_OPENGL_MAX_SAMPLERS, sampler.index, sampler.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI OpenGL backend: destroy called with an invalid or already-destroyed sampler handle");
        return;
    }
    FluxionRHIOpenGLSampler* samplerState = &s_samplers[sampler.index];
    if (samplerState->name != 0)
    {
        Fluxion_RHIOpenGL_BindingCacheForgetSampler(samplerState->name);
        glDeleteSamplers(1, &samplerState->name);
    }
    *samplerState = FluxionRHIOpenGLSampler{};
    Fluxion_RHIOpenGL_PoolFree(s_samplerSlots, FLUXION_RHI_OPENGL_MAX_SAMPLERS, sampler.index, sampler.generation);
}
