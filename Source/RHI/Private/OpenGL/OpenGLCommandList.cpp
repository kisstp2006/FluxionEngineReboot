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

// Command lists (direct/immediate execution: every
// Fluxion_RHI_CommandList_* call issues its GL call synchronously, right
// here, rather than recording into a real command-buffer object; Begin/
// End/BeginRendering/EndRendering only perform the same recording-order
// state-machine validation the Null backend does), the default-framebuffer
// swapchain, and Fence/Semaphore/QueryPool.

#include "OpenGLCommon.h"
#include "OpenGLFunctions.h"

#include <Fluxion/Application/Window/Window.h>
#include <Fluxion/Foundation/Log.h>

// --- Command list state ------------------------------------------------------

struct FluxionRHIOpenGLCommandListState
{
    FluxionRHIQueueType queueType = FLUXION_RHI_QUEUE_TYPE_GRAPHICS;
    bool recording = false;
    bool insideRendering = false;
    u32 currentPipelineIndex = FLUXION_HANDLE_INVALID_INDEX;
    GLuint currentFBO = 0; // 0 while not inside rendering, or while bound to the default framebuffer

    // The height BeginRendering was given, kept because SetViewport needs
    // it: this backend counts Y up from the bottom while the contract
    // counts it down from the top, and turning one into the other needs
    // to know how tall the whole thing is.
    u32 renderHeight = 0;
    bool use16BitIndices = false;
};

static FluxionRHIOpenGLSlot s_commandListSlots[FLUXION_RHI_OPENGL_MAX_COMMAND_LISTS];
static FluxionRHIOpenGLCommandListState s_commandListState[FLUXION_RHI_OPENGL_MAX_COMMAND_LISTS];

static bool Fluxion_RHIOpenGL_RequireRecording(FluxionRHICommandListHandle commandList, const char* what)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_commandListSlots, FLUXION_RHI_OPENGL_MAX_COMMAND_LISTS, commandList.index, commandList.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI OpenGL backend: command list handle is invalid or destroyed");
        FLUXION_UNUSED(what);
        return false;
    }
    if (!s_commandListState[commandList.index].recording)
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI OpenGL backend: command recorded outside Begin/End");
        FLUXION_UNUSED(what);
        return false;
    }
    return true;
}

FluxionRHICommandListHandle Fluxion_RHIOpenGL_CreateCommandList(FluxionRHIDeviceHandle device, FluxionRHIQueueType type)
{
    FluxionRHICommandListHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (Fluxion_RHIOpenGL_ResolveDevice(device) == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIOpenGL_PoolAllocate(s_commandListSlots, FLUXION_RHI_OPENGL_MAX_COMMAND_LISTS, &index, &generation)) return invalid;

    s_commandListState[index] = FluxionRHIOpenGLCommandListState{};
    s_commandListState[index].queueType = type;

    FluxionRHICommandListHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIOpenGL_DestroyCommandList(FluxionRHICommandListHandle commandList)
{
    Fluxion_RHIOpenGL_PoolFree(s_commandListSlots, FLUXION_RHI_OPENGL_MAX_COMMAND_LISTS, commandList.index, commandList.generation);
}

void Fluxion_RHIOpenGL_CommandListBegin(FluxionRHICommandListHandle commandList)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_commandListSlots, FLUXION_RHI_OPENGL_MAX_COMMAND_LISTS, commandList.index, commandList.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI OpenGL backend: Begin called with an invalid command list handle");
        return;
    }
    FLUXION_ASSERT_MSG(!s_commandListState[commandList.index].recording, "Fluxion RHI OpenGL backend: Begin called while already recording");
    s_commandListState[commandList.index].recording = true;
    s_commandListState[commandList.index].currentPipelineIndex = FLUXION_HANDLE_INVALID_INDEX;
}

void Fluxion_RHIOpenGL_CommandListEnd(FluxionRHICommandListHandle commandList)
{
    if (!Fluxion_RHIOpenGL_RequireRecording(commandList, "End")) return;
    FLUXION_ASSERT_MSG(!s_commandListState[commandList.index].insideRendering, "Fluxion RHI OpenGL backend: End called while still inside BeginRendering/EndRendering");
    s_commandListState[commandList.index].recording = false;
}

// --- Rendering / framebuffer -------------------------------------------------
//
// A fresh FBO is built per BeginRendering call and torn down at the
// matching EndRendering -- the simplest correct implementation given this
// backend's direct-execution model; a longer-lived FBO cache keyed by
// attachment shape is a natural follow-up if per-frame FBO churn ever
// shows up in a profile.

// Clearing depth is masked by the depth-write mask, exactly as drawing is:
// with writes switched off, a clear leaves the depth buffer as it was and
// reports nothing wrong. Any pipeline that does not write depth -- lines
// drawn over the top, anything of that sort -- leaves the mask off behind
// it, and it stays off, because the mask belongs to the context and not to
// the pass. So it is put back before every depth clear, and the state
// cache is told what really happened, or the next frame silently keeps the
// previous frame's depths and everything drawn into them disappears.
static void Fluxion_RHIOpenGL_AllowDepthClear(FluxionRHIOpenGLDevice* deviceState)
{
    if (deviceState == nullptr) return;

    FluxionRHIOpenGLStateCache* cache = &deviceState->stateCache;
    if (cache->valid && cache->depthWriteEnable) return;

    glDepthMask(GL_TRUE);
    cache->depthWriteEnable = true;
}

void Fluxion_RHIOpenGL_CommandListBeginRendering(FluxionRHICommandListHandle commandList, const FluxionRHIRenderingDesc* desc)
{
    if (!Fluxion_RHIOpenGL_RequireRecording(commandList, "BeginRendering")) return;
    FLUXION_ASSERT_MSG(!s_commandListState[commandList.index].insideRendering, "Fluxion RHI OpenGL backend: BeginRendering called twice without an EndRendering");
    s_commandListState[commandList.index].insideRendering = true;
    if (desc == nullptr) return;

    FluxionRHIOpenGLDevice* deviceState = Fluxion_RHIOpenGL_SoleDevice();
    if (deviceState == nullptr) return;

    // If the first color attachment is the default-framebuffer sentinel,
    // this frame renders straight to FBO 0 -- no
    // real GL framebuffer object is built.
    bool usesDefaultFramebuffer = false;
    if (desc->colorAttachmentCount > 0)
    {
        FluxionRHIOpenGLTextureView* view = Fluxion_RHIOpenGL_ResolveTextureView(desc->colorAttachments[0].view);
        if (view != nullptr && view->name == 0)
        {
            FluxionRHIOpenGLTexture* texture = Fluxion_RHIOpenGL_ResolveTexture(view->texture);
            usesDefaultFramebuffer = texture != nullptr && texture->isDefaultFramebufferSentinel;
        }
    }

    if (usesDefaultFramebuffer)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        s_commandListState[commandList.index].currentFBO = 0;
        if (desc->colorAttachments[0].clear)
        {
            const f32* c = desc->colorAttachments[0].clearColor;
            glClearColor(c[0], c[1], c[2], c[3]);
            GLbitfield clearBits = GL_COLOR_BUFFER_BIT;
            if (desc->depthAttachment != nullptr && desc->depthAttachment->clear)
            {
                clearBits |= GL_DEPTH_BUFFER_BIT;
                Fluxion_RHIOpenGL_AllowDepthClear(deviceState);
            }
            glClear(clearBits);
        }
        glViewport(0, 0, (GLsizei)desc->width, (GLsizei)desc->height);
        return;
    }

    GLuint fbo = 0;
    glCreateFramebuffers(1, &fbo);

    GLenum drawBuffers[FLUXION_RHI_MAX_RENDER_TARGETS];
    for (u32 i = 0; i < desc->colorAttachmentCount; ++i)
    {
        FluxionRHIOpenGLTextureView* view = Fluxion_RHIOpenGL_ResolveTextureView(desc->colorAttachments[i].view);
        if (view == nullptr) continue;
        glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0 + i, view->name, 0);
        drawBuffers[i] = GL_COLOR_ATTACHMENT0 + i;
        if (desc->colorAttachments[i].clear)
        {
            glClearNamedFramebufferfv(fbo, GL_COLOR, (GLint)i, desc->colorAttachments[i].clearColor);
        }
    }
    if (desc->colorAttachmentCount > 0) glNamedFramebufferDrawBuffers(fbo, (GLsizei)desc->colorAttachmentCount, drawBuffers);

    if (desc->depthAttachment != nullptr)
    {
        FluxionRHIOpenGLTextureView* view = Fluxion_RHIOpenGL_ResolveTextureView(desc->depthAttachment->view);
        if (view != nullptr)
        {
            glNamedFramebufferTexture(fbo, GL_DEPTH_ATTACHMENT, view->name, 0);
            if (desc->depthAttachment->clear)
            {
                f32 depthValue = desc->depthAttachment->clearColor[0];
                Fluxion_RHIOpenGL_AllowDepthClear(deviceState);
                glClearNamedFramebufferfv(fbo, GL_DEPTH, 0, &depthValue);
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, (GLsizei)desc->width, (GLsizei)desc->height);
    s_commandListState[commandList.index].currentFBO = fbo;
    s_commandListState[commandList.index].renderHeight = desc->height;
}

void Fluxion_RHIOpenGL_CommandListSetViewport(FluxionRHICommandListHandle commandList, f32 x, f32 y, f32 width, f32 height, f32 minDepth, f32 maxDepth)
{
    if (!Fluxion_RHIOpenGL_RequireRecording(commandList, "SetViewport")) return;

    // Turned upside down, because the contract counts Y from the top and
    // this backend counts it from the bottom. Everything else here is
    // already written to that contract -- see the Vulkan backend, which
    // flips for the same reason -- so the flip belongs in one place
    // rather than in every caller.
    const f32 fromBottom = (f32)s_commandListState[commandList.index].renderHeight - y - height;
    glViewport((GLint)x, (GLint)fromBottom, (GLsizei)width, (GLsizei)height);
    glDepthRange((GLdouble)minDepth, (GLdouble)maxDepth);
}

void Fluxion_RHIOpenGL_CommandListSetScissor(FluxionRHICommandListHandle commandList, i32 x, i32 y, u32 width, u32 height)
{
    if (!Fluxion_RHIOpenGL_RequireRecording(commandList, "SetScissor")) return;

    const i32 fromBottom = (i32)s_commandListState[commandList.index].renderHeight - y - (i32)height;
    glEnable(GL_SCISSOR_TEST);
    glScissor((GLint)x, (GLint)fromBottom, (GLsizei)width, (GLsizei)height);
}

void Fluxion_RHIOpenGL_CommandListEndRendering(FluxionRHICommandListHandle commandList)
{
    if (!Fluxion_RHIOpenGL_RequireRecording(commandList, "EndRendering")) return;
    FLUXION_ASSERT_MSG(s_commandListState[commandList.index].insideRendering, "Fluxion RHI OpenGL backend: EndRendering called without a matching BeginRendering");
    s_commandListState[commandList.index].insideRendering = false;

    // Off again, so a later pass that never asked for one is not quietly
    // clipped by whatever the last one set. Scissor is enabled only by
    // SetScissor and only until here.
    glDisable(GL_SCISSOR_TEST);

    GLuint fbo = s_commandListState[commandList.index].currentFBO;
    if (fbo != 0)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
    }
    s_commandListState[commandList.index].currentFBO = 0;
    s_commandListState[commandList.index].renderHeight = 0;
}

// --- Pipeline / vertex-input state -------------------------------------------

void Fluxion_RHIOpenGL_CommandListSetPipeline(FluxionRHICommandListHandle commandList, FluxionRHIPipelineHandle pipeline)
{
    if (!Fluxion_RHIOpenGL_RequireRecording(commandList, "SetPipeline")) return;
    FluxionRHIOpenGLDevice* deviceState = Fluxion_RHIOpenGL_SoleDevice();
    FluxionRHIOpenGLPipeline* pipelineState = Fluxion_RHIOpenGL_ResolvePipeline(pipeline);
    if (deviceState == nullptr || pipelineState == nullptr) return;

    glUseProgram(pipelineState->program);
    if (!pipelineState->isCompute)
    {
        glBindVertexArray(pipelineState->vao);
        Fluxion_RHIOpenGL_ApplyPipelineState(deviceState, pipelineState);
    }
    s_commandListState[commandList.index].currentPipelineIndex = pipeline.index;
}

void Fluxion_RHIOpenGL_CommandListSetVertexBuffer(FluxionRHICommandListHandle commandList, u32 slot, FluxionRHIBufferHandle buffer, usize offset)
{
    if (!Fluxion_RHIOpenGL_RequireRecording(commandList, "SetVertexBuffer")) return;
    u32 pipelineIndex = s_commandListState[commandList.index].currentPipelineIndex;
    if (pipelineIndex == FLUXION_HANDLE_INVALID_INDEX) return; // SetPipeline must happen first, matching the documented call order
    FluxionRHIOpenGLPipeline* pipelineState = Fluxion_RHIOpenGL_ResolvePipelineByIndex(pipelineIndex);
    FluxionRHIOpenGLBuffer* bufferState = Fluxion_RHIOpenGL_ResolveBuffer(buffer);
    if (pipelineState == nullptr || bufferState == nullptr) return;

    // See FluxionRHIOpenGLPipeline's attachment-cache comment: the
    // redundant re-attach is what tells a driver this buffer is dynamic.
    if (pipelineState->attachedVertexBuffer.index == buffer.index &&
        pipelineState->attachedVertexBuffer.generation == buffer.generation &&
        pipelineState->attachedVertexOffset == offset)
    {
        return;
    }
    glVertexArrayVertexBuffer(pipelineState->vao, slot, bufferState->name, (GLintptr)offset, (GLsizei)pipelineState->vertexStride);
    pipelineState->attachedVertexBuffer = buffer;
    pipelineState->attachedVertexOffset = offset;
}

void Fluxion_RHIOpenGL_CommandListSetIndexBuffer(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle buffer, usize offset, bool use16BitIndices)
{
    FLUXION_UNUSED(offset); // GL's element-buffer binding has no separate offset; the offset is applied per-Draw via the indices pointer argument
    if (!Fluxion_RHIOpenGL_RequireRecording(commandList, "SetIndexBuffer")) return;
    u32 pipelineIndex = s_commandListState[commandList.index].currentPipelineIndex;
    if (pipelineIndex == FLUXION_HANDLE_INVALID_INDEX) return;
    FluxionRHIOpenGLPipeline* pipelineState = Fluxion_RHIOpenGL_ResolvePipelineByIndex(pipelineIndex);
    FluxionRHIOpenGLBuffer* bufferState = Fluxion_RHIOpenGL_ResolveBuffer(buffer);
    if (pipelineState == nullptr || bufferState == nullptr) return;

    if (pipelineState->attachedIndexBuffer.index != buffer.index ||
        pipelineState->attachedIndexBuffer.generation != buffer.generation)
    {
        glVertexArrayElementBuffer(pipelineState->vao, bufferState->name);
        pipelineState->attachedIndexBuffer = buffer;
    }
    s_commandListState[commandList.index].use16BitIndices = use16BitIndices;
}

static GLenum Fluxion_RHIOpenGL_MapTopology(FluxionRHIPrimitiveTopology topology)
{
    switch (topology)
    {
        case FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
        case FLUXION_RHI_PRIMITIVE_TOPOLOGY_LINE_LIST: return GL_LINES;
        case FLUXION_RHI_PRIMITIVE_TOPOLOGY_POINT_LIST: return GL_POINTS;
        case FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
        default: return GL_TRIANGLES;
    }
}

// --- Draw / dispatch ---------------------------------------------------------

void Fluxion_RHIOpenGL_CommandListDraw(FluxionRHICommandListHandle commandList, u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
{
    if (!Fluxion_RHIOpenGL_RequireRecording(commandList, "Draw")) return;
    FLUXION_ASSERT_MSG(s_commandListState[commandList.index].insideRendering, "Fluxion RHI OpenGL backend: Draw called outside BeginRendering/EndRendering");
    u32 pipelineIndex = s_commandListState[commandList.index].currentPipelineIndex;
    FluxionRHIOpenGLPipeline* pipelineState = Fluxion_RHIOpenGL_ResolvePipelineByIndex(pipelineIndex);
    if (pipelineState == nullptr) return;
    GLenum topology = Fluxion_RHIOpenGL_MapTopology(pipelineState->topology);
    glDrawArraysInstancedBaseInstance(topology, (GLint)firstVertex, (GLsizei)vertexCount, (GLsizei)instanceCount, firstInstance);
}

void Fluxion_RHIOpenGL_CommandListDrawIndexed(FluxionRHICommandListHandle commandList, u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance)
{
    if (!Fluxion_RHIOpenGL_RequireRecording(commandList, "DrawIndexed")) return;
    FLUXION_ASSERT_MSG(s_commandListState[commandList.index].insideRendering, "Fluxion RHI OpenGL backend: DrawIndexed called outside BeginRendering/EndRendering");
    u32 pipelineIndex = s_commandListState[commandList.index].currentPipelineIndex;
    FluxionRHIOpenGLPipeline* pipelineState = Fluxion_RHIOpenGL_ResolvePipelineByIndex(pipelineIndex);
    if (pipelineState == nullptr) return;
    bool use16 = s_commandListState[commandList.index].use16BitIndices;
    GLenum topology = Fluxion_RHIOpenGL_MapTopology(pipelineState->topology);
    GLenum indexType = use16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    usize indexSize = use16 ? sizeof(u16) : sizeof(u32);
    void* indicesOffset = (void*)(uintptr_t)((usize)firstIndex * indexSize);
    glDrawElementsInstancedBaseVertexBaseInstance(topology, (GLsizei)indexCount, indexType, indicesOffset, (GLsizei)instanceCount, vertexOffset, firstInstance);
}

void Fluxion_RHIOpenGL_CommandListDrawIndirect(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle argsBuffer, usize offset, u32 drawCount, u32 stride)
{
    if (!Fluxion_RHIOpenGL_RequireRecording(commandList, "DrawIndirect")) return;
    FLUXION_ASSERT_MSG(s_commandListState[commandList.index].insideRendering, "Fluxion RHI OpenGL backend: DrawIndirect called outside BeginRendering/EndRendering");
    u32 pipelineIndex = s_commandListState[commandList.index].currentPipelineIndex;
    FluxionRHIOpenGLPipeline* pipelineState = Fluxion_RHIOpenGL_ResolvePipelineByIndex(pipelineIndex);
    FluxionRHIOpenGLBuffer* bufferState = Fluxion_RHIOpenGL_ResolveBuffer(argsBuffer);
    if (pipelineState == nullptr || bufferState == nullptr) return;
    GLenum topology = Fluxion_RHIOpenGL_MapTopology(pipelineState->topology);
    // glDrawArraysIndirect reads its parameters from the buffer currently
    // bound to GL_DRAW_INDIRECT_BUFFER -- bind it for the duration of this
    // one call (the RHI contract has no separate "bind indirect buffer"
    // step, so there's no persistent binding to restore afterward).
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bufferState->name);
    for (u32 i = 0; i < drawCount; ++i)
    {
        glDrawArraysIndirect(topology, (const void*)(uintptr_t)(offset + (usize)i * stride));
    }
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}

void Fluxion_RHIOpenGL_CommandListDispatch(FluxionRHICommandListHandle commandList, u32 groupCountX, u32 groupCountY, u32 groupCountZ)
{
    if (!Fluxion_RHIOpenGL_RequireRecording(commandList, "Dispatch")) return;
    glDispatchCompute(groupCountX, groupCountY, groupCountZ);
    // A dispatch's results (e.g. a storage buffer/texture read by a later
    // draw) need a barrier before they're safely visible -- conservatively
    // cover every consumer, since this backend's Barrier call (below) is
    // also conservative and callers are expected to still issue one.
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
}

// --- Copy / barrier -----------------------------------------------------

void Fluxion_RHIOpenGL_CommandListCopyBuffer(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle src, usize srcOffset, FluxionRHIBufferHandle dst, usize dstOffset, usize size)
{
    if (!Fluxion_RHIOpenGL_RequireRecording(commandList, "CopyBuffer")) return;
    FluxionRHIOpenGLBuffer* srcState = Fluxion_RHIOpenGL_ResolveBuffer(src);
    FluxionRHIOpenGLBuffer* dstState = Fluxion_RHIOpenGL_ResolveBuffer(dst);
    if (srcState == nullptr || dstState == nullptr) return;
    glCopyNamedBufferSubData(srcState->name, dstState->name, (GLintptr)srcOffset, (GLintptr)dstOffset, (GLsizeiptr)size);
}

void Fluxion_RHIOpenGL_CommandListCopyTexture(FluxionRHICommandListHandle commandList, FluxionRHITextureHandle src, FluxionRHITextureHandle dst)
{
    if (!Fluxion_RHIOpenGL_RequireRecording(commandList, "CopyTexture")) return;
    FluxionRHIOpenGLTexture* srcState = Fluxion_RHIOpenGL_ResolveTexture(src);
    FluxionRHIOpenGLTexture* dstState = Fluxion_RHIOpenGL_ResolveTexture(dst);
    if (srcState == nullptr || dstState == nullptr) return;
    u32 width = srcState->width < dstState->width ? srcState->width : dstState->width;
    u32 height = srcState->height < dstState->height ? srcState->height : dstState->height;
    u32 depth = srcState->depth < dstState->depth ? srcState->depth : dstState->depth;
    glCopyImageSubData(srcState->name, srcState->target, 0, 0, 0, 0, dstState->name, dstState->target, 0, 0, 0, 0, (GLsizei)width, (GLsizei)height, (GLsizei)depth);
}

void Fluxion_RHIOpenGL_CommandListCopyBufferToTexture(FluxionRHICommandListHandle commandList, FluxionRHIBufferHandle src, usize srcOffset, FluxionRHITextureHandle dst, u32 mipLevel, u32 arrayLayer)
{
    if (!Fluxion_RHIOpenGL_RequireRecording(commandList, "CopyBufferToTexture")) return;
    FluxionRHIOpenGLBuffer* srcState = Fluxion_RHIOpenGL_ResolveBuffer(src);
    FluxionRHIOpenGLTexture* dstState = Fluxion_RHIOpenGL_ResolveTexture(dst);
    if (srcState == nullptr || dstState == nullptr) return;

    u32 width = dstState->width >> mipLevel; if (width == 0) width = 1;
    u32 height = dstState->height >> mipLevel; if (height == 0) height = 1;

    // The contract lays rows out FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT
    // apart (see RHI.h), where a row is a row of BLOCKS -- the same thing
    // as a row of texels only for the uncompressed formats. Asked of the
    // format rather than worked out here, so this cannot disagree with
    // what the caller packed.
    const FluxionRHIFormatInfo formatInfo = Fluxion_RHI_GetFormatInfo(dstState->format);
    const usize rowBytes = Fluxion_RHI_GetFormatRowBytes(dstState->format, width);
    const usize alignedRowBytes = (rowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) / FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, srcState->name);

    if (formatInfo.compressed)
    {
        // One call per row of blocks, each handed exactly the bytes of
        // that row.
        //
        // GL can be told about a padded compressed layout through the
        // UNPACK_COMPRESSED_BLOCK_* state instead, in one call -- but then
        // the imageSize argument has to agree with a byte count the
        // driver computes from that state, and drivers have not always
        // agreed on whether the final row's padding counts. A row at a
        // time needs none of that: every call is handed a tightly packed
        // rectangle, which is the case the specification is unambiguous
        // about.
        const u32 blockRows = Fluxion_RHI_GetFormatBlockRows(dstState->format, height);
        for (u32 blockRow = 0; blockRow < blockRows; ++blockRow)
        {
            const u32 yOffset = blockRow * formatInfo.blockHeight;

            // The last row of blocks in a level whose height is not a
            // whole number of blocks still holds a whole block, but the
            // rectangle it covers stops at the edge of the level -- which
            // is the one case a sub-region of a compressed texture is
            // allowed not to be block-sized.
            const u32 rowHeight = (yOffset + formatInfo.blockHeight <= height) ? formatInfo.blockHeight : (height - yOffset);
            const usize rowOffset = srcOffset + (usize)blockRow * alignedRowBytes;

            // A cube map goes through the three-dimensional call as well,
            // with the face where a layer would be. Its target is not
            // GL_TEXTURE_2D_ARRAY, and asking only about that one sends a
            // cube down the flat path -- which this driver refuses
            // outright, six times, once per face.
            if (dstState->target == GL_TEXTURE_2D_ARRAY || dstState->target == GL_TEXTURE_CUBE_MAP)
            {
                glCompressedTextureSubImage3D(dstState->name, (GLint)mipLevel, 0, (GLint)yOffset, (GLint)arrayLayer,
                                              (GLsizei)width, (GLsizei)rowHeight, 1,
                                              Fluxion_RHIOpenGL_MapSizedInternalFormat(dstState->format),
                                              (GLsizei)rowBytes, (const void*)rowOffset);
            }
            else
            {
                glCompressedTextureSubImage2D(dstState->name, (GLint)mipLevel, 0, (GLint)yOffset,
                                              (GLsizei)width, (GLsizei)rowHeight,
                                              Fluxion_RHIOpenGL_MapSizedInternalFormat(dstState->format),
                                              (GLsizei)rowBytes, (const void*)rowOffset);
            }
        }

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        return;
    }

    GLenum pixelFormat, pixelType;
    Fluxion_RHIOpenGL_MapPixelTransferFormat(dstState->format, &pixelFormat, &pixelType);

    // glTextureSubImage2D reads its "pixels" pointer from a bound
    // GL_PIXEL_UNPACK_BUFFER as a byte offset instead of a real CPU
    // pointer whenever one is bound -- this is the DSA-era equivalent of
    // a buffer-to-image copy, since core GL has no direct analog to
    // vkCmdCopyBufferToImage.
    // UNPACK_ROW_LENGTH expresses the padded stride in texels, and is
    // reset afterwards so no later unpack inherits it.
    if (formatInfo.blockBytes != 0) glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(alignedRowBytes / formatInfo.blockBytes));

    // Same again for the uncompressed path: a cube map is uploaded a face
    // at a time through the three-dimensional call, with the face index
    // where a layer would be.
    if (dstState->target == GL_TEXTURE_2D_ARRAY || dstState->target == GL_TEXTURE_CUBE_MAP)
    {
        glTextureSubImage3D(dstState->name, (GLint)mipLevel, 0, 0, (GLint)arrayLayer, (GLsizei)width, (GLsizei)height, 1, pixelFormat, pixelType, (const void*)srcOffset);
    }
    else
    {
        glTextureSubImage2D(dstState->name, (GLint)mipLevel, 0, 0, (GLsizei)width, (GLsizei)height, pixelFormat, pixelType, (const void*)srcOffset);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void Fluxion_RHIOpenGL_CommandListCopyTextureToBuffer(FluxionRHICommandListHandle commandList, FluxionRHITextureHandle src, u32 mipLevel, u32 arrayLayer, FluxionRHIBufferHandle dst, usize dstOffset)
{
    if (!Fluxion_RHIOpenGL_RequireRecording(commandList, "CopyTextureToBuffer")) return;
    FluxionRHIOpenGLTexture* srcState = Fluxion_RHIOpenGL_ResolveTexture(src);
    FluxionRHIOpenGLBuffer* dstState = Fluxion_RHIOpenGL_ResolveBuffer(dst);
    if (srcState == nullptr || dstState == nullptr) return;

    const FluxionRHIFormatInfo formatInfo = Fluxion_RHI_GetFormatInfo(srcState->format);
    if (formatInfo.compressed)
    {
        // Said outright rather than half-done. Reading a compressed level
        // back means agreeing with the driver about how much padding the
        // final row of blocks carries, and drivers have not always agreed
        // -- see the upload path above, which sidesteps the question by
        // going a row at a time. Nothing needs this direction yet, and an
        // untested version of it would be worse than none.
        FLUXION_LOG_ERROR("RHI.OpenGL", "CopyTextureToBuffer: this backend does not read a compressed texture back; the call was dropped");
        return;
    }

    u32 width = srcState->width >> mipLevel; if (width == 0) width = 1;
    u32 height = srcState->height >> mipLevel; if (height == 0) height = 1;

    const usize rowBytes = Fluxion_RHI_GetFormatRowBytes(srcState->format, width);
    const usize alignedRowBytes = (rowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) / FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;

    GLenum pixelFormat, pixelType;
    Fluxion_RHIOpenGL_MapPixelTransferFormat(srcState->format, &pixelFormat, &pixelType);

    // PACK rather than UNPACK: the same state, for the direction that
    // writes into a buffer instead of reading out of one.
    glBindBuffer(GL_PIXEL_PACK_BUFFER, dstState->name);
    if (formatInfo.blockBytes != 0) glPixelStorei(GL_PACK_ROW_LENGTH, (GLint)(alignedRowBytes / formatInfo.blockBytes));

    // glGetTextureSubImage wants the total it is allowed to write, which
    // with the padded stride above is the padded total.
    const GLsizei bufferSize = (GLsizei)(alignedRowBytes * height);
    glGetTextureSubImage(srcState->name, (GLint)mipLevel, 0, 0, (GLint)arrayLayer, (GLsizei)width, (GLsizei)height, 1,
                         pixelFormat, pixelType, bufferSize, (void*)dstOffset);

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
}

void Fluxion_RHIOpenGL_CommandListBarrier(FluxionRHICommandListHandle commandList, const FluxionRHIBarrier* barriers, u32 barrierCount)
{
    if (!Fluxion_RHIOpenGL_RequireRecording(commandList, "Barrier")) return;
    FLUXION_UNUSED(barriers);
    if (barrierCount == 0) return;
    // OpenGL has no per-resource layout/state transition concept the way
    // Vulkan/D3D12 do -- a single conservative glMemoryBarrier(GL_ALL_BARRIER_BITS)
    // covers every documented FluxionRHIResourceState transition without
    // needing a full state->bit mapping table for a backend whose driver
    // already serializes most of this internally.
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
}

// --- Submission --------------------------------------------------------------
//
// Every CommandList_* call above already issued its GL call immediately --
// QueueSubmit only validates the command lists
// were properly ended and, since nothing is deferred, immediately marks
// the signal fence as signaled.

void Fluxion_RHIOpenGL_QueueSubmit(FluxionRHIQueueHandle queue, const FluxionRHICommandListHandle* commandLists, u32 commandListCount, FluxionRHIFenceHandle signalFence)
{
    FLUXION_UNUSED(queue);
    for (u32 i = 0; i < commandListCount; ++i)
    {
        FluxionRHICommandListHandle cl = commandLists[i];
        if (!Fluxion_RHIOpenGL_PoolIsValid(s_commandListSlots, FLUXION_RHI_OPENGL_MAX_COMMAND_LISTS, cl.index, cl.generation))
        {
            FLUXION_ASSERT_MSG(false, "Fluxion RHI OpenGL backend: Submit called with an invalid command list handle");
            continue;
        }
        FLUXION_ASSERT_MSG(!s_commandListState[cl.index].recording, "Fluxion RHI OpenGL backend: Submit called on a command list still between Begin and End");
    }
    Fluxion_RHIOpenGL_SignalFenceIfValid(signalFence);
}

// --- Swapchain --------------------------------------------------------------

struct FluxionRHIOpenGLSwapchain
{
    FluxionWindowHandle window{};
    FluxionRHITextureHandle sentinelTexture{};
#if defined(_WIN32)
    HDC hdc = nullptr;
#else
    ::Window xWindow = 0;
#endif
    u32 width = 0, height = 0;
};

static FluxionRHIOpenGLSlot s_swapchainSlots[FLUXION_RHI_OPENGL_MAX_SWAPCHAINS];
static FluxionRHIOpenGLSwapchain s_swapchains[FLUXION_RHI_OPENGL_MAX_SWAPCHAINS];

FluxionRHISwapchainHandle Fluxion_RHIOpenGL_CreateSwapchain(FluxionRHIDeviceHandle device, FluxionWindowHandle window, const FluxionRHISwapchainDesc* desc)
{
    FluxionRHISwapchainHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIOpenGLDevice* deviceState = Fluxion_RHIOpenGL_ResolveDevice(device);
    if (deviceState == nullptr || desc == nullptr || !FLUXION_HANDLE_IS_VALID(window)) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIOpenGL_PoolAllocate(s_swapchainSlots, FLUXION_RHI_OPENGL_MAX_SWAPCHAINS, &index, &generation)) return invalid;

    FluxionRHIOpenGLSwapchain* sc = &s_swapchains[index];
    *sc = FluxionRHIOpenGLSwapchain{};
    sc->window = window;
    sc->width = desc->width;
    sc->height = desc->height;

    FluxionNativeWindowHandle native = Fluxion_Window_GetNativeHandle(window);
    if (native.value == nullptr)
    {
        Fluxion_RHIOpenGL_PoolFree(s_swapchainSlots, FLUXION_RHI_OPENGL_MAX_SWAPCHAINS, index, generation);
        return invalid;
    }

#if defined(_WIN32)
    HWND hwnd = (HWND)native.value;
    HDC hdc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {};
    DescribePixelFormat(hdc, deviceState->pixelFormat, sizeof(pfd), &pfd);
    // SetPixelFormat may only be called once per HWND -- if the real
    // window's pixel format was already set to something else (or this is
    // called twice for the same window) this call simply fails, and the
    // subsequent MakeCurrent below will fail too, correctly rejecting the
    // swapchain rather than silently rendering to the wrong drawable.
    SetPixelFormat(hdc, deviceState->pixelFormat, &pfd);
    sc->hdc = hdc;
    if (!Fluxion_RHIOpenGL_MakeCurrent(deviceState, hdc))
    {
        ReleaseDC(hwnd, hdc);
        Fluxion_RHIOpenGL_PoolFree(s_swapchainSlots, FLUXION_RHI_OPENGL_MAX_SWAPCHAINS, index, generation);
        return invalid;
    }
#else
    ::Window xWindow = (::Window)(uintptr_t)native.value;
    sc->xWindow = xWindow;
    if (!Fluxion_RHIOpenGL_MakeCurrent(deviceState, xWindow))
    {
        Fluxion_RHIOpenGL_PoolFree(s_swapchainSlots, FLUXION_RHI_OPENGL_MAX_SWAPCHAINS, index, generation);
        return invalid;
    }
#endif

    if (!Fluxion_RHIOpenGL_AllocateSentinelTextureSlot(&sc->sentinelTexture))
    {
        Fluxion_RHIOpenGL_PoolFree(s_swapchainSlots, FLUXION_RHI_OPENGL_MAX_SWAPCHAINS, index, generation);
        return invalid;
    }

    FluxionRHISwapchainHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIOpenGL_DestroySwapchain(FluxionRHISwapchainHandle swapchain)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_swapchainSlots, FLUXION_RHI_OPENGL_MAX_SWAPCHAINS, swapchain.index, swapchain.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI OpenGL backend: destroy called with an invalid or already-destroyed swapchain handle");
        return;
    }
    FluxionRHIOpenGLSwapchain* sc = &s_swapchains[swapchain.index];
    Fluxion_RHIOpenGL_FreeTextureSlotDirect(sc->sentinelTexture);
#if defined(_WIN32)
    if (sc->hdc != nullptr)
    {
        FluxionNativeWindowHandle native = Fluxion_Window_GetNativeHandle(sc->window);
        if (native.value != nullptr) ReleaseDC((HWND)native.value, sc->hdc);
    }
#endif
    *sc = FluxionRHIOpenGLSwapchain{};
    Fluxion_RHIOpenGL_PoolFree(s_swapchainSlots, FLUXION_RHI_OPENGL_MAX_SWAPCHAINS, swapchain.index, swapchain.generation);
}

u32 Fluxion_RHIOpenGL_SwapchainAcquireNextImage(FluxionRHISwapchainHandle swapchain, FluxionRHISemaphoreHandle signalSemaphore)
{
    FLUXION_UNUSED(signalSemaphore); // no real acquire semantics -- semaphores are no-op placeholders in this backend
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_swapchainSlots, FLUXION_RHI_OPENGL_MAX_SWAPCHAINS, swapchain.index, swapchain.generation)) return 0;
    return 0; // WGL/GLX double-buffer automatically; there is only ever one logical "image"
}

FluxionRHITextureHandle Fluxion_RHIOpenGL_SwapchainGetTexture(FluxionRHISwapchainHandle swapchain, u32 imageIndex)
{
    FLUXION_UNUSED(imageIndex);
    FluxionRHITextureHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_swapchainSlots, FLUXION_RHI_OPENGL_MAX_SWAPCHAINS, swapchain.index, swapchain.generation)) return invalid;
    return s_swapchains[swapchain.index].sentinelTexture;
}

void Fluxion_RHIOpenGL_SwapchainPresent(FluxionRHISwapchainHandle swapchain, u32 imageIndex, FluxionRHISemaphoreHandle waitSemaphore)
{
    FLUXION_UNUSED(imageIndex);
    FLUXION_UNUSED(waitSemaphore);
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_swapchainSlots, FLUXION_RHI_OPENGL_MAX_SWAPCHAINS, swapchain.index, swapchain.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI OpenGL backend: Present called with an invalid swapchain handle");
        return;
    }
    FluxionRHIOpenGLSwapchain* sc = &s_swapchains[swapchain.index];
#if defined(_WIN32)
    if (sc->hdc != nullptr) SwapBuffers(sc->hdc);
#else
    FluxionRHIOpenGLDevice* deviceState = Fluxion_RHIOpenGL_SoleDevice();
    if (deviceState != nullptr && deviceState->display != nullptr) glXSwapBuffers(deviceState->display, sc->xWindow);
#endif
}

void Fluxion_RHIOpenGL_SwapchainGetExtent(FluxionRHISwapchainHandle swapchain, u32* outWidth, u32* outHeight)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_swapchainSlots, FLUXION_RHI_OPENGL_MAX_SWAPCHAINS, swapchain.index, swapchain.generation))
    {
        if (outWidth) *outWidth = 0;
        if (outHeight) *outHeight = 0;
        return;
    }
    // Tracks the live window size on every query -- the render target
    // size only needs to track the current window size, and
    // WGL/GLX need no swapchain object recreation on
    // resize, unlike a real Vulkan swapchain.
    FluxionRHIOpenGLSwapchain* sc = &s_swapchains[swapchain.index];
    u32 width = 0, height = 0;
    Fluxion_Window_GetSize(sc->window, &width, &height);
    if (width > 0 && height > 0)
    {
        sc->width = width;
        sc->height = height;
    }
    if (outWidth) *outWidth = sc->width;
    if (outHeight) *outHeight = sc->height;
}

// --- Synchronization --------------------------------------------------------

static FluxionRHIOpenGLSlot s_fenceSlots[FLUXION_RHI_OPENGL_MAX_FENCES];
static bool s_fenceSignaled[FLUXION_RHI_OPENGL_MAX_FENCES];

void Fluxion_RHIOpenGL_SignalFenceIfValid(FluxionRHIFenceHandle fence)
{
    if (FLUXION_HANDLE_IS_VALID(fence) && Fluxion_RHIOpenGL_PoolIsValid(s_fenceSlots, FLUXION_RHI_OPENGL_MAX_FENCES, fence.index, fence.generation))
    {
        s_fenceSignaled[fence.index] = true;
    }
}

FluxionRHIFenceHandle Fluxion_RHIOpenGL_CreateFence(FluxionRHIDeviceHandle device, bool signaled)
{
    FluxionRHIFenceHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (Fluxion_RHIOpenGL_ResolveDevice(device) == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIOpenGL_PoolAllocate(s_fenceSlots, FLUXION_RHI_OPENGL_MAX_FENCES, &index, &generation)) return invalid;
    s_fenceSignaled[index] = signaled;

    FluxionRHIFenceHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIOpenGL_DestroyFence(FluxionRHIFenceHandle fence)
{
    Fluxion_RHIOpenGL_PoolFree(s_fenceSlots, FLUXION_RHI_OPENGL_MAX_FENCES, fence.index, fence.generation);
}

bool Fluxion_RHIOpenGL_WaitForFence(FluxionRHIFenceHandle fence)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_fenceSlots, FLUXION_RHI_OPENGL_MAX_FENCES, fence.index, fence.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI OpenGL backend: WaitForFence called with an invalid fence handle");
        return false;
    }
    // Every GL call up to and including the Submit that would signal this
    // fence has already been issued synchronously --
    // a glFinish ensures the driver has actually completed them GPU-side
    // before this call returns, matching what a caller expects "wait"
    // to mean.
    glFinish();
    s_fenceSignaled[fence.index] = true;
    return true;
}

void Fluxion_RHIOpenGL_ResetFence(FluxionRHIFenceHandle fence)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_fenceSlots, FLUXION_RHI_OPENGL_MAX_FENCES, fence.index, fence.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI OpenGL backend: ResetFence called with an invalid fence handle");
        return;
    }
    s_fenceSignaled[fence.index] = false;
}

static FluxionRHIOpenGLSlot s_semaphoreSlots[FLUXION_RHI_OPENGL_MAX_SEMAPHORES];

FluxionRHISemaphoreHandle Fluxion_RHIOpenGL_CreateSemaphore(FluxionRHIDeviceHandle device)
{
    FluxionRHISemaphoreHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (Fluxion_RHIOpenGL_ResolveDevice(device) == nullptr) return invalid;
    u32 index, generation;
    if (!Fluxion_RHIOpenGL_PoolAllocate(s_semaphoreSlots, FLUXION_RHI_OPENGL_MAX_SEMAPHORES, &index, &generation)) return invalid;
    FluxionRHISemaphoreHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIOpenGL_DestroySemaphore(FluxionRHISemaphoreHandle semaphore)
{
    Fluxion_RHIOpenGL_PoolFree(s_semaphoreSlots, FLUXION_RHI_OPENGL_MAX_SEMAPHORES, semaphore.index, semaphore.generation);
}

struct FluxionRHIOpenGLQueryPool
{
    GLuint* queries = nullptr;
    u32 count = 0;
};

static FluxionRHIOpenGLSlot s_queryPoolSlots[FLUXION_RHI_OPENGL_MAX_QUERY_POOLS];
static FluxionRHIOpenGLQueryPool s_queryPools[FLUXION_RHI_OPENGL_MAX_QUERY_POOLS];

FluxionRHIQueryPoolHandle Fluxion_RHIOpenGL_CreateQueryPool(FluxionRHIDeviceHandle device, u32 queryCount)
{
    FluxionRHIQueryPoolHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (Fluxion_RHIOpenGL_ResolveDevice(device) == nullptr || queryCount == 0) return invalid;
    u32 index, generation;
    if (!Fluxion_RHIOpenGL_PoolAllocate(s_queryPoolSlots, FLUXION_RHI_OPENGL_MAX_QUERY_POOLS, &index, &generation)) return invalid;

    s_queryPools[index].queries = new GLuint[queryCount];
    s_queryPools[index].count = queryCount;
    glGenQueries((GLsizei)queryCount, s_queryPools[index].queries);

    FluxionRHIQueryPoolHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIOpenGL_DestroyQueryPool(FluxionRHIQueryPoolHandle queryPool)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_queryPoolSlots, FLUXION_RHI_OPENGL_MAX_QUERY_POOLS, queryPool.index, queryPool.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI OpenGL backend: destroy called with an invalid or already-destroyed query pool handle");
        return;
    }
    FluxionRHIOpenGLQueryPool* pool = &s_queryPools[queryPool.index];
    if (pool->queries != nullptr)
    {
        glDeleteQueries((GLsizei)pool->count, pool->queries);
        delete[] pool->queries;
    }
    *pool = FluxionRHIOpenGLQueryPool{};
    Fluxion_RHIOpenGL_PoolFree(s_queryPoolSlots, FLUXION_RHI_OPENGL_MAX_QUERY_POOLS, queryPool.index, queryPool.generation);
}

void Fluxion_RHIOpenGL_CommandListResetQueryPool(FluxionRHICommandListHandle commandList, FluxionRHIQueryPoolHandle queryPool, u32 firstQuery, u32 queryCount)
{
    // Nothing to do: a GL query object needs no reset between uses --
    // glQueryCounter simply overwrites its value. The call exists so a
    // caller can write one portable frame loop.
    FLUXION_UNUSED(commandList); FLUXION_UNUSED(queryPool); FLUXION_UNUSED(firstQuery); FLUXION_UNUSED(queryCount);
}

void Fluxion_RHIOpenGL_CommandListWriteTimestamp(FluxionRHICommandListHandle commandList, FluxionRHIQueryPoolHandle queryPool, u32 queryIndex)
{
    // Executes immediately, like every other command in this backend.
    FLUXION_UNUSED(commandList);
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_queryPoolSlots, FLUXION_RHI_OPENGL_MAX_QUERY_POOLS, queryPool.index, queryPool.generation)) return;
    FluxionRHIOpenGLQueryPool* pool = &s_queryPools[queryPool.index];
    if (queryIndex >= pool->count || glQueryCounter == nullptr) return;
    glQueryCounter(pool->queries[queryIndex], GL_TIMESTAMP);
}

bool Fluxion_RHIOpenGL_QueryPoolGetResults(FluxionRHIQueryPoolHandle queryPool, u32 firstQuery, u32 queryCount, u64* outTicks)
{
    if (outTicks == nullptr || queryCount == 0 || glGetQueryObjectui64v == nullptr) return false;
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_queryPoolSlots, FLUXION_RHI_OPENGL_MAX_QUERY_POOLS, queryPool.index, queryPool.generation)) return false;
    FluxionRHIOpenGLQueryPool* pool = &s_queryPools[queryPool.index];
    if (firstQuery + queryCount > pool->count) return false;

    // GL_QUERY_RESULT blocks until the value exists, which in this
    // backend it already does by the time a caller sensibly asks -- the
    // fence it waited on was a glFinish.
    for (u32 i = 0; i < queryCount; ++i)
    {
        GLuint64 value = 0;
        glGetQueryObjectui64v(pool->queries[firstQuery + i], GL_QUERY_RESULT, &value);
        outTicks[i] = (u64)value;
    }
    return true;
}

u64 Fluxion_RHIOpenGL_GetTimestampFrequency(FluxionRHIDeviceHandle device)
{
    if (Fluxion_RHIOpenGL_ResolveDevice(device) == nullptr) return 0;
    return 1000000000ull; // GL_TIMESTAMP is defined to report nanoseconds
}
