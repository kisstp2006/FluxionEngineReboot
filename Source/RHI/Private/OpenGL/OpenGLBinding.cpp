// BindGroupLayout / BindGroup + CommandList_SetBindGroup. See design
// decision #3 in the orchestrating task description: bindings are
// partitioned into the global GL binding-point namespace with a fixed
// per-group stride, and a small binding cache skips redundant glBind*
// calls.

#include "OpenGLCommon.h"
#include "OpenGLFunctions.h"

// --- BindGroupLayout (CPU-side desc only, no real GL object -- see design
// decision #3: "BindGroupLayout objects don't need a real GL object") ------

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

// --- SetBindGroup ------------------------------------------------------------

void Fluxion_RHIOpenGL_CommandListSetBindGroup(FluxionRHICommandListHandle commandList, u32 groupIndex, FluxionRHIBindGroupHandle bindGroup)
{
    FLUXION_UNUSED(commandList); // GL binding state is process-global (single implicit context), see design decision #1
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
                if (cache->boundSampler[slot] != name)
                {
                    glBindSampler(slot, name);
                    cache->boundSampler[slot] = name;
                }
                break;
            }
        }
    }
}
