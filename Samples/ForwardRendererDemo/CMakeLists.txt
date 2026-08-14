add_executable(ForwardRendererDemo main.cpp)
target_link_libraries(ForwardRendererDemo PRIVATE Fluxion::RHI Fluxion::Application Fluxion::Platform Fluxion::Foundation Fluxion::ShaderCompiler Fluxion::RenderCore Fluxion::Scene Fluxion::Script Fluxion::Core)
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

# The sample is the only thing that exercises a real device end to end --
# every test above it runs on the Null backend. A tree where each module
# passes its own checks and the assembled program does not start is one
# nothing else here would notice.
#
# Deliberately narrow: it draws a few frames and exits cleanly, which is
# enough to cover device and swapchain creation, shader compilation,
# pipeline creation, a real submit and present, and the whole shutdown
# path including saving the pipeline cache. It asserts nothing about what
# the frames look like -- that needs eyes, and a check that pretended
# otherwise would be worse than none.
add_test(NAME ForwardRendererDemoSmoke
         COMMAND ForwardRendererDemo --graphics=vulkan --frames=30)

# A machine with no GPU, no display, or no Vulkan loader cannot answer the
# question this asks, and failing there would mean "your environment"
# rather than "your change". The sample reports that case by exiting 77,
# which ctest reads as skipped.
set_tests_properties(ForwardRendererDemoSmoke PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 120)
