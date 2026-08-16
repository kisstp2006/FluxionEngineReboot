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

// The validation layer: a vtable that checks and then forwards. With
// validation off it is absent, not disabled.
//
// Two rules: (1) it REPORTS, never asserts -- tests drive broken calls in
// and assert on the count. (2) An INVALID call is not forwarded (the
// backend would crash on it), but a merely SUSPICIOUS one is: the caller
// may know something the tracker does not, and behavior must not differ
// with validation on.
//
// The shadow state tracks live handles, last-declared resource states,
// and each command list's begin/render/end lifecycle.

#include "RHIBackendVTable.h"

#include <Fluxion/Foundation/Log.h>

#include <string.h>

// Generous fixed caps -- larger than any backend pool, so an index that
// exceeds them is itself proof of a corrupt handle.
#define FLUXION_RHIVAL_MAX_OBJECTS 4096
#define FLUXION_RHIVAL_MAX_COMMAND_LISTS 256

typedef struct FluxionRHIValidationPool
{
    bool alive[FLUXION_RHIVAL_MAX_OBJECTS];
    u32 generation[FLUXION_RHIVAL_MAX_OBJECTS];
} FluxionRHIValidationPool;

static FluxionRHIValidationPool s_buffers;
static FluxionRHIValidationPool s_textures;
static FluxionRHIValidationPool s_textureViews;
static FluxionRHIValidationPool s_samplers;
static FluxionRHIValidationPool s_shaders;
static FluxionRHIValidationPool s_pipelines;
static FluxionRHIValidationPool s_bindGroupLayouts;
static FluxionRHIValidationPool s_bindGroups;
static FluxionRHIValidationPool s_fences;
static FluxionRHIValidationPool s_semaphores;
static FluxionRHIValidationPool s_queryPools;
static FluxionRHIValidationPool s_commandLists;

static FluxionRHIResourceState s_bufferState[FLUXION_RHIVAL_MAX_OBJECTS];
static FluxionRHIResourceState s_textureState[FLUXION_RHIVAL_MAX_OBJECTS];
static bool s_pipelineIsCompute[FLUXION_RHIVAL_MAX_OBJECTS];

typedef struct FluxionRHIValidationCmdState
{
    bool recording;
    bool insideRendering;
    bool hasPipeline;
    bool pipelineIsCompute;
} FluxionRHIValidationCmdState;

static FluxionRHIValidationCmdState s_cmdState[FLUXION_RHIVAL_MAX_COMMAND_LISTS];

static const FluxionRHIBackendVTable* s_real = NULL;
static FluxionRHIBackendVTable s_wrapped;
static u64 s_errorCount = 0;

u64 Fluxion_RHI_Validation_GetErrorCount(void)
{
    return s_errorCount;
}

void Fluxion_RHI_Validation_ResetErrorCount(void)
{
    s_errorCount = 0;
}

static void Fluxion_RHIValidation_Report(const char* message)
{
    ++s_errorCount;
    FLUXION_LOG_ERROR("RHI.Validation", "%s", message);
}

// --- Shadow pool bookkeeping ---------------------------------------------

static bool Fluxion_RHIValidation_IsLive(const FluxionRHIValidationPool* pool, u32 index, u32 generation)
{
    return index < FLUXION_RHIVAL_MAX_OBJECTS && pool->alive[index] && pool->generation[index] == generation;
}

static void Fluxion_RHIValidation_OnCreate(FluxionRHIValidationPool* pool, u32 index, u32 generation)
{
    if (index >= FLUXION_RHIVAL_MAX_OBJECTS) return;
    pool->alive[index] = true;
    pool->generation[index] = generation;
}

static void Fluxion_RHIValidation_OnDestroy(FluxionRHIValidationPool* pool, u32 index)
{
    if (index >= FLUXION_RHIVAL_MAX_OBJECTS) return;
    pool->alive[index] = false;
}

// Shared shape of every destroy check: a dead or foreign handle is
// reported and the call is dropped before the backend's own assert can
// turn it into a stop. Returns whether the call may proceed.
static bool Fluxion_RHIValidation_CheckDestroy(FluxionRHIValidationPool* pool, u32 index, u32 generation, const char* what)
{
    if (!Fluxion_RHIValidation_IsLive(pool, index, generation))
    {
        Fluxion_RHIValidation_Report(what);
        return false;
    }
    Fluxion_RHIValidation_OnDestroy(pool, index);
    return true;
}

static FluxionRHIValidationCmdState* Fluxion_RHIValidation_ResolveCmd(FluxionRHICommandListHandle commandList)
{
    if (!Fluxion_RHIValidation_IsLive(&s_commandLists, commandList.index, commandList.generation)) return NULL;
    if (commandList.index >= FLUXION_RHIVAL_MAX_COMMAND_LISTS) return NULL;
    return &s_cmdState[commandList.index];
}

// --- Resources -------------------------------------------------------------

static FluxionRHIBufferHandle Fluxion_RHIValidation_CreateBuffer(FluxionRHIDeviceHandle device, const FluxionRHIBufferDesc* desc)
{
    FluxionRHIBufferHandle handle = s_real->CreateBuffer(device, desc);
    if (FLUXION_HANDLE_IS_VALID(handle))
    {
        Fluxion_RHIValidation_OnCreate(&s_buffers, handle.index, handle.generation);
        if (handle.index < FLUXION_RHIVAL_MAX_OBJECTS) s_bufferState[handle.index] = FLUXION_RHI_RESOURCE_STATE_COMMON;
    }
    return handle;
}

static void Fluxion_RHIValidation_DestroyBuffer(FluxionRHIBufferHandle buffer)
{
    if (!Fluxion_RHIValidation_CheckDestroy(&s_buffers, buffer.index, buffer.generation,
        "DestroyBuffer: the handle is invalid, already destroyed, or was never created -- the call was dropped")) return;
    s_real->DestroyBuffer(buffer);
}

static void* Fluxion_RHIValidation_MapBuffer(FluxionRHIBufferHandle buffer)
{
    if (!Fluxion_RHIValidation_IsLive(&s_buffers, buffer.index, buffer.generation))
    {
        Fluxion_RHIValidation_Report("MapBuffer: the buffer handle is invalid or already destroyed");
        return NULL;
    }
    return s_real->MapBuffer(buffer);
}

static void Fluxion_RHIValidation_UnmapBuffer(FluxionRHIBufferHandle buffer)
{
    if (!Fluxion_RHIValidation_IsLive(&s_buffers, buffer.index, buffer.generation))
    {
        Fluxion_RHIValidation_Report("UnmapBuffer: the buffer handle is invalid or already destroyed");
        return;
    }
    s_real->UnmapBuffer(buffer);
}

static FluxionRHITextureHandle Fluxion_RHIValidation_CreateTexture(FluxionRHIDeviceHandle device, const FluxionRHITextureDesc* desc)
{
    FluxionRHITextureHandle handle = s_real->CreateTexture(device, desc);
    if (FLUXION_HANDLE_IS_VALID(handle))
    {
        Fluxion_RHIValidation_OnCreate(&s_textures, handle.index, handle.generation);
        if (handle.index < FLUXION_RHIVAL_MAX_OBJECTS) s_textureState[handle.index] = FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
    }
    return handle;
}

static void Fluxion_RHIValidation_DestroyTexture(FluxionRHITextureHandle texture)
{
    if (!Fluxion_RHIValidation_CheckDestroy(&s_textures, texture.index, texture.generation,
        "DestroyTexture: the handle is invalid, already destroyed, or was never created -- the call was dropped")) return;
    s_real->DestroyTexture(texture);
}

static FluxionRHITextureViewHandle Fluxion_RHIValidation_CreateTextureView(FluxionRHIDeviceHandle device, const FluxionRHITextureViewDesc* desc)
{
    if (desc != NULL && !Fluxion_RHIValidation_IsLive(&s_textures, desc->texture.index, desc->texture.generation))
    {
        FluxionRHITextureViewHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
        Fluxion_RHIValidation_Report("CreateTextureView: the texture being viewed is invalid or already destroyed");
        return invalid;
    }
    FluxionRHITextureViewHandle handle = s_real->CreateTextureView(device, desc);
    if (FLUXION_HANDLE_IS_VALID(handle)) Fluxion_RHIValidation_OnCreate(&s_textureViews, handle.index, handle.generation);
    return handle;
}

static void Fluxion_RHIValidation_DestroyTextureView(FluxionRHITextureViewHandle view)
{
    if (!Fluxion_RHIValidation_CheckDestroy(&s_textureViews, view.index, view.generation,
        "DestroyTextureView: the handle is invalid, already destroyed, or was never created -- the call was dropped")) return;
    s_real->DestroyTextureView(view);
}

static FluxionRHISamplerHandle Fluxion_RHIValidation_CreateSampler(FluxionRHIDeviceHandle device, const FluxionRHISamplerDesc* desc)
{
    FluxionRHISamplerHandle handle = s_real->CreateSampler(device, desc);
    if (FLUXION_HANDLE_IS_VALID(handle)) Fluxion_RHIValidation_OnCreate(&s_samplers, handle.index, handle.generation);
    return handle;
}

static void Fluxion_RHIValidation_DestroySampler(FluxionRHISamplerHandle sampler)
{
    if (!Fluxion_RHIValidation_CheckDestroy(&s_samplers, sampler.index, sampler.generation,
        "DestroySampler: the handle is invalid, already destroyed, or was never created -- the call was dropped")) return;
    s_real->DestroySampler(sampler);
}

// --- Shaders / pipelines ---------------------------------------------------

static FluxionRHIShaderHandle Fluxion_RHIValidation_CreateShader(FluxionRHIDeviceHandle device, const FluxionRHIShaderDesc* desc)
{
    FluxionRHIShaderHandle handle = s_real->CreateShader(device, desc);
    if (FLUXION_HANDLE_IS_VALID(handle)) Fluxion_RHIValidation_OnCreate(&s_shaders, handle.index, handle.generation);
    return handle;
}

static void Fluxion_RHIValidation_DestroyShader(FluxionRHIShaderHandle shader)
{
    if (!Fluxion_RHIValidation_CheckDestroy(&s_shaders, shader.index, shader.generation,
        "DestroyShader: the handle is invalid, already destroyed, or was never created -- the call was dropped")) return;
    s_real->DestroyShader(shader);
}

static FluxionRHIPipelineHandle Fluxion_RHIValidation_CreateGraphicsPipeline(FluxionRHIDeviceHandle device, const FluxionRHIGraphicsPipelineDesc* desc)
{
    FluxionRHIPipelineHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (desc != NULL)
    {
        if (!Fluxion_RHIValidation_IsLive(&s_shaders, desc->vertexShader.index, desc->vertexShader.generation) ||
            !Fluxion_RHIValidation_IsLive(&s_shaders, desc->fragmentShader.index, desc->fragmentShader.generation))
        {
            Fluxion_RHIValidation_Report("CreateGraphicsPipeline: a shader in the desc is invalid or already destroyed");
            return invalid;
        }
    }
    FluxionRHIPipelineHandle handle = s_real->CreateGraphicsPipeline(device, desc);
    if (FLUXION_HANDLE_IS_VALID(handle))
    {
        Fluxion_RHIValidation_OnCreate(&s_pipelines, handle.index, handle.generation);
        if (handle.index < FLUXION_RHIVAL_MAX_OBJECTS) s_pipelineIsCompute[handle.index] = false;
    }
    return handle;
}

static FluxionRHIPipelineHandle Fluxion_RHIValidation_CreateComputePipeline(FluxionRHIDeviceHandle device, const FluxionRHIComputePipelineDesc* desc)
{
    FluxionRHIPipelineHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (desc != NULL && !Fluxion_RHIValidation_IsLive(&s_shaders, desc->computeShader.index, desc->computeShader.generation))
    {
        Fluxion_RHIValidation_Report("CreateComputePipeline: the compute shader in the desc is invalid or already destroyed");
        return invalid;
    }
    FluxionRHIPipelineHandle handle = s_real->CreateComputePipeline(device, desc);
    if (FLUXION_HANDLE_IS_VALID(handle))
    {
        Fluxion_RHIValidation_OnCreate(&s_pipelines, handle.index, handle.generation);
        if (handle.index < FLUXION_RHIVAL_MAX_OBJECTS) s_pipelineIsCompute[handle.index] = true;
    }
    return handle;
}

static void Fluxion_RHIValidation_DestroyPipeline(FluxionRHIPipelineHandle pipeline)
{
    if (!Fluxion_RHIValidation_CheckDestroy(&s_pipelines, pipeline.index, pipeline.generation,
        "DestroyPipeline: the handle is invalid, already destroyed, or was never created -- the call was dropped")) return;
    s_real->DestroyPipeline(pipeline);
}

// --- Bind groups -----------------------------------------------------------

static FluxionRHIBindGroupLayoutHandle Fluxion_RHIValidation_CreateBindGroupLayout(FluxionRHIDeviceHandle device, const FluxionRHIBindGroupLayoutDesc* desc)
{
    FluxionRHIBindGroupLayoutHandle handle = s_real->CreateBindGroupLayout(device, desc);
    if (FLUXION_HANDLE_IS_VALID(handle)) Fluxion_RHIValidation_OnCreate(&s_bindGroupLayouts, handle.index, handle.generation);
    return handle;
}

static void Fluxion_RHIValidation_DestroyBindGroupLayout(FluxionRHIBindGroupLayoutHandle layout)
{
    // Layouts are deduped by shape in some backends: a second Create can
    // return the SAME handle, and the matching second Destroy is legal.
    // The shadow cannot tell that apart from a double free without
    // refcounting the dedupe itself, so layouts get liveness recording
    // but no destroy refusal -- better no check than a false report.
    s_real->DestroyBindGroupLayout(layout);
}

static FluxionRHIBindGroupHandle Fluxion_RHIValidation_CreateBindGroup(FluxionRHIDeviceHandle device, const FluxionRHIBindGroupDesc* desc)
{
    FluxionRHIBindGroupHandle handle = s_real->CreateBindGroup(device, desc);
    if (FLUXION_HANDLE_IS_VALID(handle)) Fluxion_RHIValidation_OnCreate(&s_bindGroups, handle.index, handle.generation);
    return handle;
}

static void Fluxion_RHIValidation_DestroyBindGroup(FluxionRHIBindGroupHandle bindGroup)
{
    if (!Fluxion_RHIValidation_CheckDestroy(&s_bindGroups, bindGroup.index, bindGroup.generation,
        "DestroyBindGroup: the handle is invalid, already destroyed, or was never created -- the call was dropped")) return;
    s_real->DestroyBindGroup(bindGroup);
}

// --- Command lists ---------------------------------------------------------

static FluxionRHICommandListHandle Fluxion_RHIValidation_CreateCommandList(FluxionRHIDeviceHandle device, FluxionRHIQueueType type)
{
    FluxionRHICommandListHandle handle = s_real->CreateCommandList(device, type);
    if (FLUXION_HANDLE_IS_VALID(handle) && handle.index < FLUXION_RHIVAL_MAX_COMMAND_LISTS)
    {
        Fluxion_RHIValidation_OnCreate(&s_commandLists, handle.index, handle.generation);
        memset(&s_cmdState[handle.index], 0, sizeof(s_cmdState[handle.index]));
    }
    return handle;
}

static void Fluxion_RHIValidation_DestroyCommandList(FluxionRHICommandListHandle commandList)
{
    if (!Fluxion_RHIValidation_CheckDestroy(&s_commandLists, commandList.index, commandList.generation,
        "DestroyCommandList: the handle is invalid, already destroyed, or was never created -- the call was dropped")) return;
    s_real->DestroyCommandList(commandList);
}

static void Fluxion_RHIValidation_CommandListBegin(FluxionRHICommandListHandle commandList)
{
    FluxionRHIValidationCmdState* state = Fluxion_RHIValidation_ResolveCmd(commandList);
    if (state == NULL)
    {
        Fluxion_RHIValidation_Report("CommandList_Begin: the command list handle is invalid or already destroyed");
        return;
    }
    if (state->recording)
    {
        Fluxion_RHIValidation_Report("CommandList_Begin: the command list is already recording -- End was never called");
        return;
    }
    state->recording = true;
    state->insideRendering = false;
    state->hasPipeline = false;
    s_real->CommandListBegin(commandList);
}

static void Fluxion_RHIValidation_CommandListEnd(FluxionRHICommandListHandle commandList)
{
    FluxionRHIValidationCmdState* state = Fluxion_RHIValidation_ResolveCmd(commandList);
    if (state == NULL)
    {
        Fluxion_RHIValidation_Report("CommandList_End: the command list handle is invalid or already destroyed");
        return;
    }
    if (!state->recording)
    {
        Fluxion_RHIValidation_Report("CommandList_End: the command list is not recording -- Begin was never called");
        return;
    }
    if (state->insideRendering)
    {
        Fluxion_RHIValidation_Report("CommandList_End: still inside BeginRendering/EndRendering -- the pass was never ended");
        return;
    }
    state->recording = false;
    s_real->CommandListEnd(commandList);
}

static void Fluxion_RHIValidation_CommandListBeginRendering(FluxionRHICommandListHandle commandList, const FluxionRHIRenderingDesc* desc)
{
    FluxionRHIValidationCmdState* state = Fluxion_RHIValidation_ResolveCmd(commandList);
    if (state == NULL || !state->recording)
    {
        Fluxion_RHIValidation_Report("CommandList_BeginRendering: the command list is not recording");
        return;
    }
    if (state->insideRendering)
    {
        Fluxion_RHIValidation_Report("CommandList_BeginRendering: already inside a rendering pass -- passes do not nest");
        return;
    }
    state->insideRendering = true;
    s_real->CommandListBeginRendering(commandList, desc);
}

static void Fluxion_RHIValidation_CommandListEndRendering(FluxionRHICommandListHandle commandList)
{
    FluxionRHIValidationCmdState* state = Fluxion_RHIValidation_ResolveCmd(commandList);
    if (state == NULL || !state->recording || !state->insideRendering)
    {
        Fluxion_RHIValidation_Report("CommandList_EndRendering: not inside a rendering pass");
        return;
    }
    state->insideRendering = false;
    s_real->CommandListEndRendering(commandList);
}

static void Fluxion_RHIValidation_CommandListSetPipeline(FluxionRHICommandListHandle commandList, FluxionRHIPipelineHandle pipeline)
{
    FluxionRHIValidationCmdState* state = Fluxion_RHIValidation_ResolveCmd(commandList);
    if (state == NULL || !state->recording)
    {
        Fluxion_RHIValidation_Report("CommandList_SetPipeline: the command list is not recording");
        return;
    }
    if (!Fluxion_RHIValidation_IsLive(&s_pipelines, pipeline.index, pipeline.generation))
    {
        Fluxion_RHIValidation_Report("CommandList_SetPipeline: the pipeline handle is invalid or already destroyed");
        return;
    }
    state->hasPipeline = true;
    state->pipelineIsCompute = pipeline.index < FLUXION_RHIVAL_MAX_OBJECTS && s_pipelineIsCompute[pipeline.index];
    s_real->CommandListSetPipeline(commandList, pipeline);
}

static bool Fluxion_RHIValidation_CheckDraw(FluxionRHICommandListHandle commandList, const char* what)
{
    FluxionRHIValidationCmdState* state = Fluxion_RHIValidation_ResolveCmd(commandList);
    if (state == NULL || !state->recording)
    {
        Fluxion_RHIValidation_Report(what);
        return false;
    }
    if (!state->insideRendering)
    {
        Fluxion_RHIValidation_Report("Draw: outside BeginRendering/EndRendering -- there is no render pass to draw into");
        return false;
    }
    if (!state->hasPipeline || state->pipelineIsCompute)
    {
        Fluxion_RHIValidation_Report("Draw: no graphics pipeline is bound -- a draw with no pipeline (or a compute one) renders nothing defined");
        return false;
    }
    return true;
}

static void Fluxion_RHIValidation_CommandListDraw(FluxionRHICommandListHandle commandList, u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance)
{
    if (!Fluxion_RHIValidation_CheckDraw(commandList, "Draw: the command list is not recording")) return;
    s_real->CommandListDraw(commandList, vertexCount, instanceCount, firstVertex, firstInstance);
}

static void Fluxion_RHIValidation_CommandListDrawIndexed(FluxionRHICommandListHandle commandList, u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance)
{
    if (!Fluxion_RHIValidation_CheckDraw(commandList, "DrawIndexed: the command list is not recording")) return;
    s_real->CommandListDrawIndexed(commandList, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

static void Fluxion_RHIValidation_CommandListDispatch(FluxionRHICommandListHandle commandList, u32 groupCountX, u32 groupCountY, u32 groupCountZ)
{
    FluxionRHIValidationCmdState* state = Fluxion_RHIValidation_ResolveCmd(commandList);
    if (state == NULL || !state->recording)
    {
        Fluxion_RHIValidation_Report("Dispatch: the command list is not recording");
        return;
    }
    if (!state->hasPipeline || !state->pipelineIsCompute)
    {
        Fluxion_RHIValidation_Report("Dispatch: no compute pipeline is bound -- a dispatch through a graphics pipeline is undefined");
        return;
    }
    s_real->CommandListDispatch(commandList, groupCountX, groupCountY, groupCountZ);
}

static void Fluxion_RHIValidation_CommandListSetBindGroup(FluxionRHICommandListHandle commandList, u32 groupIndex, FluxionRHIBindGroupHandle bindGroup)
{
    FluxionRHIValidationCmdState* state = Fluxion_RHIValidation_ResolveCmd(commandList);
    if (state == NULL || !state->recording)
    {
        Fluxion_RHIValidation_Report("SetBindGroup: the command list is not recording");
        return;
    }
    if (groupIndex >= FLUXION_RHI_MAX_BIND_GROUPS)
    {
        Fluxion_RHIValidation_Report("SetBindGroup: the group index is out of range");
        return;
    }
    if (!Fluxion_RHIValidation_IsLive(&s_bindGroups, bindGroup.index, bindGroup.generation))
    {
        Fluxion_RHIValidation_Report("SetBindGroup: the bind group handle is invalid or already destroyed");
        return;
    }
    s_real->CommandListSetBindGroup(commandList, groupIndex, bindGroup);
}

static void Fluxion_RHIValidation_CommandListBarrier(FluxionRHICommandListHandle commandList, const FluxionRHIBarrier* barriers, u32 barrierCount)
{
    FluxionRHIValidationCmdState* state = Fluxion_RHIValidation_ResolveCmd(commandList);
    if (state == NULL || !state->recording)
    {
        Fluxion_RHIValidation_Report("Barrier: the command list is not recording");
        return;
    }

    for (u32 i = 0; barriers != NULL && i < barrierCount; ++i)
    {
        const FluxionRHIBarrier* barrier = &barriers[i];
        if (FLUXION_HANDLE_IS_VALID(barrier->texture))
        {
            // Textures that arrived from a swapchain were never seen by
            // CreateTexture here, so an untracked index is left alone --
            // state checking is only meaningful for what this layer
            // watched from birth.
            if (Fluxion_RHIValidation_IsLive(&s_textures, barrier->texture.index, barrier->texture.generation) &&
                barrier->texture.index < FLUXION_RHIVAL_MAX_OBJECTS)
            {
                FluxionRHIResourceState tracked = s_textureState[barrier->texture.index];
                // Declared UNDEFINED is always acceptable: it means
                // "discard, whatever was there", and every backend
                // honors that reading.
                if (barrier->before != FLUXION_RHI_RESOURCE_STATE_UNDEFINED && barrier->before != tracked)
                {
                    Fluxion_RHIValidation_Report("Barrier: a texture's declared before-state disagrees with the state it was last transitioned to (forwarded anyway -- see the log for both states)");
                    FLUXION_LOG_ERROR("RHI.Validation", "  declared before=%d, tracked=%d", (int)barrier->before, (int)tracked);
                }
                s_textureState[barrier->texture.index] = barrier->after;
            }
        }
        else if (FLUXION_HANDLE_IS_VALID(barrier->buffer))
        {
            if (Fluxion_RHIValidation_IsLive(&s_buffers, barrier->buffer.index, barrier->buffer.generation) &&
                barrier->buffer.index < FLUXION_RHIVAL_MAX_OBJECTS)
            {
                FluxionRHIResourceState tracked = s_bufferState[barrier->buffer.index];
                if (barrier->before != FLUXION_RHI_RESOURCE_STATE_UNDEFINED &&
                    barrier->before != FLUXION_RHI_RESOURCE_STATE_COMMON &&
                    barrier->before != tracked)
                {
                    Fluxion_RHIValidation_Report("Barrier: a buffer's declared before-state disagrees with the state it was last transitioned to (forwarded anyway -- see the log for both states)");
                    FLUXION_LOG_ERROR("RHI.Validation", "  declared before=%d, tracked=%d", (int)barrier->before, (int)tracked);
                }
                s_bufferState[barrier->buffer.index] = barrier->after;
            }
            else if (!Fluxion_RHIValidation_IsLive(&s_buffers, barrier->buffer.index, barrier->buffer.generation))
            {
                Fluxion_RHIValidation_Report("Barrier: a buffer in the barrier list is invalid or already destroyed");
            }
        }
    }

    s_real->CommandListBarrier(commandList, barriers, barrierCount);
}

// --- Submission ------------------------------------------------------------

static void Fluxion_RHIValidation_QueueSubmit(FluxionRHIQueueHandle queue, const FluxionRHICommandListHandle* commandLists, u32 commandListCount, FluxionRHIFenceHandle signalFence)
{
    for (u32 i = 0; commandLists != NULL && i < commandListCount; ++i)
    {
        FluxionRHIValidationCmdState* state = Fluxion_RHIValidation_ResolveCmd(commandLists[i]);
        if (state == NULL)
        {
            Fluxion_RHIValidation_Report("Queue_Submit: a command list in the submission is invalid or already destroyed -- the submit was dropped");
            return;
        }
        if (state->recording)
        {
            Fluxion_RHIValidation_Report("Queue_Submit: a command list in the submission is still recording -- End it first; the submit was dropped");
            return;
        }
    }
    s_real->QueueSubmit(queue, commandLists, commandListCount, signalFence);
}

// --- Swapchain / sync ------------------------------------------------------

static FluxionRHITextureHandle Fluxion_RHIValidation_SwapchainGetTexture(FluxionRHISwapchainHandle swapchain, u32 imageIndex)
{
    FluxionRHITextureHandle handle = s_real->SwapchainGetTexture(swapchain, imageIndex);
    // Adopted rather than reported: this texture was born inside the
    // backend, and the caller will legitimately view it and barrier it.
    if (FLUXION_HANDLE_IS_VALID(handle) && !Fluxion_RHIValidation_IsLive(&s_textures, handle.index, handle.generation))
    {
        Fluxion_RHIValidation_OnCreate(&s_textures, handle.index, handle.generation);
        if (handle.index < FLUXION_RHIVAL_MAX_OBJECTS) s_textureState[handle.index] = FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
    }
    return handle;
}

static FluxionRHIFenceHandle Fluxion_RHIValidation_CreateFence(FluxionRHIDeviceHandle device, bool signaled)
{
    FluxionRHIFenceHandle handle = s_real->CreateFence(device, signaled);
    if (FLUXION_HANDLE_IS_VALID(handle)) Fluxion_RHIValidation_OnCreate(&s_fences, handle.index, handle.generation);
    return handle;
}

static void Fluxion_RHIValidation_DestroyFence(FluxionRHIFenceHandle fence)
{
    if (!Fluxion_RHIValidation_CheckDestroy(&s_fences, fence.index, fence.generation,
        "DestroyFence: the handle is invalid, already destroyed, or was never created -- the call was dropped")) return;
    s_real->DestroyFence(fence);
}

static FluxionRHISemaphoreHandle Fluxion_RHIValidation_CreateSemaphore(FluxionRHIDeviceHandle device)
{
    FluxionRHISemaphoreHandle handle = s_real->CreateSemaphore(device);
    if (FLUXION_HANDLE_IS_VALID(handle)) Fluxion_RHIValidation_OnCreate(&s_semaphores, handle.index, handle.generation);
    return handle;
}

static void Fluxion_RHIValidation_DestroySemaphore(FluxionRHISemaphoreHandle semaphore)
{
    if (!Fluxion_RHIValidation_CheckDestroy(&s_semaphores, semaphore.index, semaphore.generation,
        "DestroySemaphore: the handle is invalid, already destroyed, or was never created -- the call was dropped")) return;
    s_real->DestroySemaphore(semaphore);
}

static FluxionRHIQueryPoolHandle Fluxion_RHIValidation_CreateQueryPool(FluxionRHIDeviceHandle device, u32 queryCount)
{
    FluxionRHIQueryPoolHandle handle = s_real->CreateQueryPool(device, queryCount);
    if (FLUXION_HANDLE_IS_VALID(handle)) Fluxion_RHIValidation_OnCreate(&s_queryPools, handle.index, handle.generation);
    return handle;
}

static void Fluxion_RHIValidation_DestroyQueryPool(FluxionRHIQueryPoolHandle queryPool)
{
    if (!Fluxion_RHIValidation_CheckDestroy(&s_queryPools, queryPool.index, queryPool.generation,
        "DestroyQueryPool: the handle is invalid, already destroyed, or was never created -- the call was dropped")) return;
    s_real->DestroyQueryPool(queryPool);
}

// --- Install / remove ------------------------------------------------------

const FluxionRHIBackendVTable* Fluxion_RHIValidation_Wrap(const FluxionRHIBackendVTable* real)
{
    s_real = real;

    // Everything not explicitly validated passes through untouched --
    // copying the whole table first is what makes the wrapper complete
    // by construction: a vtable entry added later is passed through
    // rather than silently lost.
    s_wrapped = *real;

    s_wrapped.CreateBuffer = Fluxion_RHIValidation_CreateBuffer;
    s_wrapped.DestroyBuffer = Fluxion_RHIValidation_DestroyBuffer;
    s_wrapped.MapBuffer = Fluxion_RHIValidation_MapBuffer;
    s_wrapped.UnmapBuffer = Fluxion_RHIValidation_UnmapBuffer;
    s_wrapped.CreateTexture = Fluxion_RHIValidation_CreateTexture;
    s_wrapped.DestroyTexture = Fluxion_RHIValidation_DestroyTexture;
    s_wrapped.CreateTextureView = Fluxion_RHIValidation_CreateTextureView;
    s_wrapped.DestroyTextureView = Fluxion_RHIValidation_DestroyTextureView;
    s_wrapped.CreateSampler = Fluxion_RHIValidation_CreateSampler;
    s_wrapped.DestroySampler = Fluxion_RHIValidation_DestroySampler;
    s_wrapped.CreateShader = Fluxion_RHIValidation_CreateShader;
    s_wrapped.DestroyShader = Fluxion_RHIValidation_DestroyShader;
    s_wrapped.CreateGraphicsPipeline = Fluxion_RHIValidation_CreateGraphicsPipeline;
    s_wrapped.CreateComputePipeline = Fluxion_RHIValidation_CreateComputePipeline;
    s_wrapped.DestroyPipeline = Fluxion_RHIValidation_DestroyPipeline;
    s_wrapped.CreateBindGroupLayout = Fluxion_RHIValidation_CreateBindGroupLayout;
    s_wrapped.DestroyBindGroupLayout = Fluxion_RHIValidation_DestroyBindGroupLayout;
    s_wrapped.CreateBindGroup = Fluxion_RHIValidation_CreateBindGroup;
    s_wrapped.DestroyBindGroup = Fluxion_RHIValidation_DestroyBindGroup;
    s_wrapped.CreateCommandList = Fluxion_RHIValidation_CreateCommandList;
    s_wrapped.DestroyCommandList = Fluxion_RHIValidation_DestroyCommandList;
    s_wrapped.CommandListBegin = Fluxion_RHIValidation_CommandListBegin;
    s_wrapped.CommandListEnd = Fluxion_RHIValidation_CommandListEnd;
    s_wrapped.CommandListBeginRendering = Fluxion_RHIValidation_CommandListBeginRendering;
    s_wrapped.CommandListEndRendering = Fluxion_RHIValidation_CommandListEndRendering;
    s_wrapped.CommandListSetPipeline = Fluxion_RHIValidation_CommandListSetPipeline;
    s_wrapped.CommandListDraw = Fluxion_RHIValidation_CommandListDraw;
    s_wrapped.CommandListDrawIndexed = Fluxion_RHIValidation_CommandListDrawIndexed;
    s_wrapped.CommandListDispatch = Fluxion_RHIValidation_CommandListDispatch;
    s_wrapped.CommandListSetBindGroup = Fluxion_RHIValidation_CommandListSetBindGroup;
    s_wrapped.CommandListBarrier = Fluxion_RHIValidation_CommandListBarrier;
    s_wrapped.QueueSubmit = Fluxion_RHIValidation_QueueSubmit;
    s_wrapped.SwapchainGetTexture = Fluxion_RHIValidation_SwapchainGetTexture;
    s_wrapped.CreateFence = Fluxion_RHIValidation_CreateFence;
    s_wrapped.DestroyFence = Fluxion_RHIValidation_DestroyFence;
    s_wrapped.CreateSemaphore = Fluxion_RHIValidation_CreateSemaphore;
    s_wrapped.DestroySemaphore = Fluxion_RHIValidation_DestroySemaphore;
    s_wrapped.CreateQueryPool = Fluxion_RHIValidation_CreateQueryPool;
    s_wrapped.DestroyQueryPool = Fluxion_RHIValidation_DestroyQueryPool;

    return &s_wrapped;
}

void Fluxion_RHIValidation_Unwrap(void)
{
    s_real = NULL;
    memset(&s_buffers, 0, sizeof(s_buffers));
    memset(&s_textures, 0, sizeof(s_textures));
    memset(&s_textureViews, 0, sizeof(s_textureViews));
    memset(&s_samplers, 0, sizeof(s_samplers));
    memset(&s_shaders, 0, sizeof(s_shaders));
    memset(&s_pipelines, 0, sizeof(s_pipelines));
    memset(&s_bindGroupLayouts, 0, sizeof(s_bindGroupLayouts));
    memset(&s_bindGroups, 0, sizeof(s_bindGroups));
    memset(&s_fences, 0, sizeof(s_fences));
    memset(&s_semaphores, 0, sizeof(s_semaphores));
    memset(&s_queryPools, 0, sizeof(s_queryPools));
    memset(&s_commandLists, 0, sizeof(s_commandLists));
    memset(s_cmdState, 0, sizeof(s_cmdState));
}
