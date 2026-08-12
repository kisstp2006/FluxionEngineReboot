add_executable(VulkanTriangleDemo main.cpp)
target_link_libraries(VulkanTriangleDemo PRIVATE Fluxion::RHI Fluxion::Application Fluxion::Platform Fluxion::Foundation Fluxion::ShaderCompiler)
engine_set_warnings(VulkanTriangleDemo)

target_compile_definitions(VulkanTriangleDemo PRIVATE
    FLUXION_DEMO_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
)
