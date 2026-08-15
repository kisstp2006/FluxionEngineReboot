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
            // No flags at all: storage the device owns outright.
            //
            // GL_DYNAMIC_STORAGE_BIT would say the client may write into
            // this buffer directly, and it is the one thing that must not
            // be said here. Nothing does -- everything reaching a buffer
            // of this class arrives through a server-side copy from a
            // staging buffer, and the call that would need the flag is
            // never made anywhere in this backend. Saying it anyway is
            // not free: a driver told a vertex or index buffer might be
            // written from the client keeps it where the client can be
            // served quickly, which means host memory, which means every
            // draw reads its geometry across the bus.
            //
            // A driver may still emit a one-time "moved from VIDEO to
            // HOST" performance note about small buffers of this class,
            // between the first submitted frame and the second, during
            // its own residency pass. That decision was probed from
            // every side this backend controls -- these storage flags,
            // re-attaching versus caching the VAO attachment, whether
            // the copy source was mapped during the transfer, client-
            // storage on the staging side, and the buffer's size -- and
            // none of them changes it; measured GPU frame time matches
            // the other backends throughout. The note is the driver
            // narrating a placement choice it owns, not something this
            // code causes, and it is deliberately left visible rather
            // than filtered: a log rule that hides one vendor's noise
            // eventually hides another vendor's real complaint.
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
    if (bufferState->name != 0) glDeleteBuffers(1, &bufferState->name);
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
    if (textureState->name != 0 && !textureState->isDefaultFramebufferSentinel) glDeleteTextures(1, &textureState->name);
    *textureState = FluxionRHIOpenGLTexture{};
    Fluxion_RHIOpenGL_PoolFree(s_textureSlots, FLUXION_RHI_OPENGL_MAX_TEXTURES, texture.index, texture.generation);
}

bool Fluxion_RHIOpenGL_AllocateSentinelTextureSlot(FluxionRHITextureHandle* outHandle)
{
    u32 index, generation;
    if (!Fluxion_RHIOpenGL_PoolAllocate(s_textureSlots, FLUXION_RHI_OPENGL_MAX_TEXTURES, &index, &generation)) return false;
    s_textures[index] = FluxionRHIOpenGLTexture{};
    s_textures[index].isDefaultFramebufferSentinel = true;
    outHandle->index = index;
    outHandle->generation = generation;
    return true;
}

void Fluxion_RHIOpenGL_FreeTextureSlotDirect(FluxionRHITextureHandle handle)
{
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

    if (textureState->isDefaultFramebufferSentinel)
    {
        // The default-framebuffer sentinel has no real GL texture object
        // -- its view is likewise a sentinel,
        // recognized the same way by CommandListBeginRendering.
        viewState->name = 0;
        viewState->target = 0;
        viewState->ownsName = false;
    }
    else
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
            glCreateTextures(textureState->target, 1, &viewName); // glTextureView requires a pre-created (unstorage'd) name
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

void Fluxion_RHIOpenGL_DestroyTextureView(FluxionRHITextureViewHandle view)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_textureViewSlots, FLUXION_RHI_OPENGL_MAX_TEXTURE_VIEWS, view.index, view.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI OpenGL backend: destroy called with an invalid or already-destroyed texture view handle");
        return;
    }
    FluxionRHIOpenGLTextureView* viewState = &s_textureViews[view.index];
    if (viewState->ownsName && viewState->name != 0) glDeleteTextures(1, &viewState->name);
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
    if (samplerState->name != 0) glDeleteSamplers(1, &samplerState->name);
    *samplerState = FluxionRHIOpenGLSampler{};
    Fluxion_RHIOpenGL_PoolFree(s_samplerSlots, FLUXION_RHI_OPENGL_MAX_SAMPLERS, sampler.index, sampler.generation);
}
