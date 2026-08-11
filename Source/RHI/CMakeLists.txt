find_package(Vulkan REQUIRED)

engine_add_module(
    NAME RHI
    TYPE STATIC
    LANGUAGE C CXX
    PUBLIC_DEPENDENCIES
        Fluxion::Foundation
        Fluxion::Application
    PRIVATE_DEPENDENCIES
        Fluxion::Platform
)

# Vulkan::Vulkan/vma are PRIVATE: the public RHI headers never mention a
# Vulkan or VMA type (Rule 26/28 in ARCHITECTURE_RULES.md -- backend-native
# graphics types never leak into the portable API), so nothing outside
# Private/Vulkan needs these include paths or link inputs.
target_link_libraries(RHI PRIVATE Vulkan::Vulkan vma)
