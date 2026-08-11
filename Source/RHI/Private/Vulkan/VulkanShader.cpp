// Shader modules. The RHI contract only ever hands this backend raw
// SPIR-V bytecode (already compiled offline, e.g. via glslc) -- no
// shader compiler lives in this file or anywhere in the RHI module.
// Deferred destruction applies here too, same as buffers/textures (see
// the retirement-queue comment in VulkanCommon.h).

#include "VulkanCommon.h"

#include <cstring>

#define FLUXION_RHI_VULKAN_SHADER_MAX_ENTRY_POINT 63

struct FluxionRHIVulkanShader
{
    VkShaderModule module = VK_NULL_HANDLE;
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
    char entryPoint[FLUXION_RHI_VULKAN_SHADER_MAX_ENTRY_POINT + 1] = "main";
};

static FluxionRHIVulkanSlot s_shaderSlots[FLUXION_RHI_VULKAN_MAX_SHADERS];
static FluxionRHIVulkanShader s_shaders[FLUXION_RHI_VULKAN_MAX_SHADERS];

static VkShaderStageFlagBits Fluxion_RHIVulkan_MapShaderStage(FluxionRHIShaderStage stage)
{
    switch (stage)
    {
        case FLUXION_RHI_SHADER_STAGE_FRAGMENT: return VK_SHADER_STAGE_FRAGMENT_BIT;
        case FLUXION_RHI_SHADER_STAGE_COMPUTE: return VK_SHADER_STAGE_COMPUTE_BIT;
        default: return VK_SHADER_STAGE_VERTEX_BIT;
    }
}

FluxionRHIShaderHandle Fluxion_RHIVulkan_CreateShader(FluxionRHIDeviceHandle device, const FluxionRHIShaderDesc* desc)
{
    FluxionRHIShaderHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_ResolveDevice(device);
    if (deviceState == nullptr || desc == nullptr || desc->bytecode == nullptr || desc->bytecodeSize == 0) return invalid;

    u32 index, generation;
    if (!Fluxion_RHIVulkan_PoolAllocate(s_shaderSlots, FLUXION_RHI_VULKAN_MAX_SHADERS, &index, &generation)) return invalid;

    VkShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = desc->bytecodeSize;
    moduleInfo.pCode = (const u32*)desc->bytecode;

    FluxionRHIVulkanShader* shader = &s_shaders[index];
    *shader = FluxionRHIVulkanShader{};
    shader->stage = Fluxion_RHIVulkan_MapShaderStage(desc->stage);
    if (desc->entryPoint != nullptr)
    {
#if defined(_MSC_VER)
        strncpy_s(shader->entryPoint, sizeof(shader->entryPoint), desc->entryPoint, FLUXION_RHI_VULKAN_SHADER_MAX_ENTRY_POINT);
#else
        std::strncpy(shader->entryPoint, desc->entryPoint, FLUXION_RHI_VULKAN_SHADER_MAX_ENTRY_POINT);
#endif
    }

    if (vkCreateShaderModule(deviceState->device, &moduleInfo, nullptr, &shader->module) != VK_SUCCESS)
    {
        Fluxion_RHIVulkan_PoolFree(s_shaderSlots, FLUXION_RHI_VULKAN_MAX_SHADERS, index, generation);
        return invalid;
    }

    FluxionRHIShaderHandle handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

void Fluxion_RHIVulkan_FinalizeShaderSlot(u32 index)
{
    FluxionRHIVulkanShader* shader = &s_shaders[index];
    if (shader->module != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(Fluxion_RHIVulkan_GetOwningDevice(), shader->module, nullptr);
    }
    *shader = FluxionRHIVulkanShader{};
}

void Fluxion_RHIVulkan_DestroyShader(FluxionRHIShaderHandle shader)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_shaderSlots, FLUXION_RHI_VULKAN_MAX_SHADERS, shader.index, shader.generation))
    {
        FLUXION_ASSERT_MSG(false, "Fluxion RHI Vulkan backend: DestroyShader called with an invalid or already-destroyed handle");
        return;
    }
    Fluxion_RHIVulkan_PoolFree(s_shaderSlots, FLUXION_RHI_VULKAN_MAX_SHADERS, shader.index, shader.generation);
    FluxionRHIVulkanDevice* deviceState = Fluxion_RHIVulkan_SoleDevice();
    if (deviceState != nullptr)
        Fluxion_RHIVulkan_Retire(deviceState, FluxionRHIVulkanRetiredEntry::Kind::Shader, shader.index, deviceState->gcCounter);
    else
        Fluxion_RHIVulkan_FinalizeShaderSlot(shader.index);
}

VkShaderModule Fluxion_RHIVulkan_ResolveShaderModule(FluxionRHIShaderHandle shader)
{
    if (!Fluxion_RHIVulkan_PoolIsValid(s_shaderSlots, FLUXION_RHI_VULKAN_MAX_SHADERS, shader.index, shader.generation)) return VK_NULL_HANDLE;
    return s_shaders[shader.index].module;
}

VkShaderStageFlagBits Fluxion_RHIVulkan_ResolveShaderStage(FluxionRHIShaderHandle shader)
{
    return s_shaders[shader.index].stage;
}

const char* Fluxion_RHIVulkan_ResolveShaderEntryPoint(FluxionRHIShaderHandle shader)
{
    return s_shaders[shader.index].entryPoint;
}
