add_executable(ForwardRendererDemo main.cpp)
target_link_libraries(ForwardRendererDemo PRIVATE Fluxion::RHI Fluxion::Application Fluxion::Platform Fluxion::Foundation Fluxion::ShaderCompiler Fluxion::RenderCore)
engine_set_warnings(ForwardRendererDemo)

target_compile_definitions(ForwardRendererDemo PRIVATE
    FLUXION_DEMO_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
)
