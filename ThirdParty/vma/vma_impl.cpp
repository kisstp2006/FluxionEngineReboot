// Vendored VMA (https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
// is a single header, implementation-included-once library: exactly one
// translation unit must define VMA_IMPLEMENTATION before including it. This
// is that translation unit -- kept in ThirdParty so the RHI module's own
// Vulkan backend files never need to worry about which one owns the
// implementation.
// Both default to 1 in the vendored header; leaving DYNAMIC on requires
// the caller to also hand VMA its own vkGetInstanceProcAddr/
// vkGetDeviceProcAddr via VmaAllocatorCreateInfo::pVulkanFunctions (an
// extra step Fluxion's Vulkan backend doesn't do -- it links directly
// against the Vulkan loader import library already). Forcing STATIC-only
// here means vmaCreateAllocator works with the plain
// VmaAllocatorCreateInfo{physicalDevice, device, instance} the backend
// constructs, with no separate function-pointer wiring needed.
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
