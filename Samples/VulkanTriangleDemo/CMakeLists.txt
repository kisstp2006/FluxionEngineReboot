add_executable(VulkanTriangleDemo main.c)
target_link_libraries(VulkanTriangleDemo PRIVATE Fluxion::RHI Fluxion::Application Fluxion::Platform Fluxion::Foundation)
engine_set_warnings(VulkanTriangleDemo)
