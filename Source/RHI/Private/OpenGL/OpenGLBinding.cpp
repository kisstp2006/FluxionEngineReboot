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

// BindGroupLayout / BindGroup + CommandList_SetBindGroup:
// bindings are
// partitioned into the global GL binding-point namespace with a fixed
// per-group stride, and a small binding cache skips redundant glBind*
// calls.

#include "OpenGLCommon.h"
#include "OpenGLFunctions.h"

// --- BindGroupLayout (CPU-side desc only, no real GL object) ---------------

static FluxionRHIOpenGLSlot s_bindGroupLayoutSlots[FLUXION_RHI_OPENGL_MAX_BIND_GROUP_LAYOUTS];
static FluxionRHIBindGroupLayoutDesc s_bindGroupLayouts[FLUXION_RHI_OPENGL_MAX_BIND_GROUP_LAYOUTS];

const FluxionRHIBindGroupLayoutDesc* Fluxion_RHIOpenGL_ResolveBindGroupLayout(FluxionRHIBindGroupLayoutHandle layout)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_bindGroupLayoutSlots, FLUXION_RHI_OPENGL_MAX_BIND_GROUP_LAYOUTS, layout.index, layout.generation)) return nullptr;
    return &s_bindGroupLayouts[layout.index];
}

FluxionRHIBindGroupLayoutHandle Fluxion_RHIOpenGL_CreateBindGroupLayout(FluxionRHIDeviceHandle device, const FluxionRHIBindGroupLayoutDesc* desc)
{
    FluxionRHIBindGroupLayoutHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (Fluxion_RHIOpenGL_ResolveDevice(device) == nullptr || desc == nullptr) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIOpenGL_PoolAllocate(s_bindGroupLayoutSlots, FLUXION_RHI_OPENGL_MAX_BIND_GROUP_LAYOUTS, &index, &generation)) return invalid;

    s_bindGroupLayouts[index] = *desc;

    FluxionRHIBindGroupLayoutHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIOpenGL_DestroyBindGroupLayout(FluxionRHIBindGroupLayoutHandle layout)
{
    Fluxion_RHIOpenGL_PoolFree(s_bindGroupLayoutSlots, FLUXION_RHI_OPENGL_MAX_BIND_GROUP_LAYOUTS, layout.index, layout.generation);
}

// --- BindGroup (concrete resource list) -------------------------------------

static FluxionRHIOpenGLSlot s_bindGroupSlots[FLUXION_RHI_OPENGL_MAX_BIND_GROUPS];
static FluxionRHIOpenGLBindGroup s_bindGroups[FLUXION_RHI_OPENGL_MAX_BIND_GROUPS];

FluxionRHIOpenGLBindGroup* Fluxion_RHIOpenGL_ResolveBindGroup(FluxionRHIBindGroupHandle bindGroup)
{
    if (!Fluxion_RHIOpenGL_PoolIsValid(s_bindGroupSlots, FLUXION_RHI_OPENGL_MAX_BIND_GROUPS, bindGroup.index, bindGroup.generation)) return nullptr;
    return &s_bindGroups[bindGroup.index];
}

FluxionRHIBindGroupHandle Fluxion_RHIOpenGL_CreateBindGroup(FluxionRHIDeviceHandle device, const FluxionRHIBindGroupDesc* desc)
{
    FluxionRHIBindGroupHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FLUXION_UNUSED(device);
    if (desc == nullptr || Fluxion_RHIOpenGL_ResolveBindGroupLayout(desc->layout) == nullptr) return invalid;
    if (desc->entryCount > FLUXION_RHI_MAX_BIND_GROUP_ENTRIES) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIOpenGL_PoolAllocate(s_bindGroupSlots, FLUXION_RHI_OPENGL_MAX_BIND_GROUPS, &index, &generation)) return invalid;

    FluxionRHIOpenGLBindGroup* groupState = &s_bindGroups[index];
    *groupState = FluxionRHIOpenGLBindGroup{};
    groupState->layout = desc->layout;
    groupState->entryCount = desc->entryCount;
    for (u32 i = 0; i < desc->entryCount; ++i) groupState->entries[i] = desc->entries[i];

    FluxionRHIBindGroupHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIOpenGL_DestroyBindGroup(FluxionRHIBindGroupHandle bindGroup)
{
    Fluxion_RHIOpenGL_PoolFree(s_bindGroupSlots, FLUXION_RHI_OPENGL_MAX_BIND_GROUPS, bindGroup.index, bindGroup.generation);
}

// --- The binding cache, told about deletions ---------------------------------
//
// Deleting a GL object silently unbinds it from every binding point of
// the current context, and GL recycles the freed name for the next
// object made. A cache that was not told still holds that name -- so the
// next SetBindGroup with the recycled name compares equal, skips the
// bind, and the draw samples an EMPTY unit. Zeros, not an error, which
// is the worst way for it to come out.

void Fluxion_RHIOpenGL_BindingCacheForgetTexture(GLuint name)
{
    FluxionRHIOpenGLDevice* deviceState = Fluxion_RHIOpenGL_SoleDevice();
    if (deviceState == nullptr || name == 0) return;
    for (u32 i = 0; i < FLUXION_RHIOPENGL_TOTAL_BINDING_SLOTS; ++i)
    {
        if (deviceState->bindingCache.boundTexture[i] == name) deviceState->bindingCache.boundTexture[i] = 0;
    }
}

void Fluxion_RHIOpenGL_BindingCacheForgetSampler(GLuint name)
{
    FluxionRHIOpenGLDevice* deviceState = Fluxion_RHIOpenGL_SoleDevice();
    if (deviceState == nullptr || name == 0) return;
    for (u32 i = 0; i < FLUXION_RHIOPENGL_TOTAL_BINDING_SLOTS; ++i)
    {
        if (deviceState->bindingCache.boundSampler[i] == name) deviceState->bindingCache.boundSampler[i] = 0;
    }
}

void Fluxion_RHIOpenGL_BindingCacheForgetBuffer(GLuint name)
{
    FluxionRHIOpenGLDevice* deviceState = Fluxion_RHIOpenGL_SoleDevice();
    if (deviceState == nullptr || name == 0) return;
    for (u32 i = 0; i < FLUXION_RHIOPENGL_TOTAL_BINDING_SLOTS; ++i)
    {
        if (deviceState->bindingCache.boundUBO[i] == name) deviceState->bindingCache.boundUBO[i] = 0;
        if (deviceState->bindingCache.boundSSBO[i] == name) deviceState->bindingCache.boundSSBO[i] = 0;
    }
}

// --- SetBindGroup ------------------------------------------------------------

void Fluxion_RHIOpenGL_CommandListSetBindGroup(FluxionRHICommandListHandle commandList, u32 groupIndex, FluxionRHIBindGroupHandle bindGroup)
{
    FLUXION_UNUSED(commandList); // GL binding state is process-global (single implicit context)
    FluxionRHIOpenGLDevice* deviceState = Fluxion_RHIOpenGL_SoleDevice();
    if (deviceState == nullptr || groupIndex >= FLUXION_RHI_MAX_BIND_GROUPS) return;

    FluxionRHIOpenGLBindGroup* groupState = Fluxion_RHIOpenGL_ResolveBindGroup(bindGroup);
    if (groupState == nullptr) return;

    FluxionRHIOpenGLBindingCache* cache = &deviceState->bindingCache;

    for (u32 i = 0; i < groupState->entryCount; ++i)
    {
        const FluxionRHIBindGroupEntry* entry = &groupState->entries[i];
        u32 slot = groupIndex * FLUXION_RHIOPENGL_BINDINGS_PER_GROUP + entry->binding;
        if (slot >= FLUXION_RHIOPENGL_TOTAL_BINDING_SLOTS) continue;

        switch (entry->type)
        {
            case FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER:
            case FLUXION_RHI_BINDING_TYPE_STORAGE_BUFFER:
            {
                FluxionRHIOpenGLBuffer* bufferState = Fluxion_RHIOpenGL_ResolveBuffer(entry->buffer);
                GLuint name = bufferState != nullptr ? bufferState->name : 0;
                GLenum target = entry->type == FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER ? GL_UNIFORM_BUFFER : GL_SHADER_STORAGE_BUFFER;
                GLuint* cached = entry->type == FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER ? &cache->boundUBO[slot] : &cache->boundSSBO[slot];
                usize offset = entry->bufferOffset;
                usize size = entry->bufferSize > 0 ? entry->bufferSize : (bufferState != nullptr ? bufferState->size : 0);
                // The binding cache here only tracks the bound object
                // (not offset/size) -- glBindBufferRange is still issued
                // every call, since an offset/size-only change on the
                // same buffer object still needs a fresh call; the cache
                // slot is kept up to date so a future extension (e.g.
                // tracking the full range) has the bookkeeping in place.
                glBindBufferRange(target, slot, name, (GLintptr)offset, (GLsizeiptr)(size > 0 ? size : 1));
                *cached = name;
                break;
            }
            case FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE:
            {
                FluxionRHIOpenGLTextureView* viewState = Fluxion_RHIOpenGL_ResolveTextureView(entry->textureView);
                GLuint name = viewState != nullptr ? viewState->name : 0;
                if (cache->boundTexture[slot] != name)
                {
                    glBindTextureUnit(slot, name);
                    cache->boundTexture[slot] = name;
                }
                break;
            }
            case FLUXION_RHI_BINDING_TYPE_SAMPLER:
            {
                FluxionRHIOpenGLSampler* samplerState = Fluxion_RHIOpenGL_ResolveSampler(entry->sampler);
                GLuint name = samplerState != nullptr ? samplerState->name : 0;

                // ONTO THE TEXTURE'S UNIT, not its own. A texture and its
                // sampler are two bindings everywhere else and one object
                // here, and the shader this compiles to names the pair by
                // the TEXTURE's number -- so a sampler object left on its
                // own unit is a sampler nothing reads, and the texture is
                // sampled by whatever parameters it carries itself.
                //
                // Harmless for an ordinary texture, which carries the same
                // parameters. Not harmless for a shadow map: comparison
                // lives on the sampler alone, and a depth texture read by
                // a comparison sampler that has comparison switched off is
                // undefined -- which the driver says out loud.
                const u32 pairedSlot = entry->binding > 0 ? slot - 1 : slot;
                if (cache->boundSampler[pairedSlot] != name)
                {
                    glBindSampler(pairedSlot, name);
                    cache->boundSampler[pairedSlot] = name;
                }
                break;
            }
        }
    }
}
