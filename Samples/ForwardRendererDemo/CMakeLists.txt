add_executable(ForwardRendererDemo main.cpp)
target_link_libraries(ForwardRendererDemo PRIVATE Fluxion::RHI Fluxion::Application Fluxion::Platform Fluxion::Foundation Fluxion::ShaderCompiler Fluxion::RenderCore Fluxion::Scene Fluxion::Script)
engine_set_warnings(ForwardRendererDemo)

target_compile_definitions(ForwardRendererDemo PRIVATE
    FLUXION_DEMO_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
    FLUXION_DEMO_SCRIPT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Scripts"
    # Under the build tree rather than beside the sources: a checkout is
    # never written to, and deleting the build tree is all it takes to be
    # rid of what has accumulated here.
    FLUXION_DEMO_SCRIPT_CACHE_DIR="${CMAKE_CURRENT_BINARY_DIR}/ScriptCache"
    FLUXION_DEMO_SHADER_CACHE_DIR="${CMAKE_CURRENT_BINARY_DIR}/ShaderCache"
)
