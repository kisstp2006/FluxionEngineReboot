find_package(Vulkan REQUIRED)
find_package(OpenGL REQUIRED)

# D3D12 is a Windows-only API -- Private/D3D12/*.cpp links against
# d3d12.lib/dxgi.lib and D3D12MA, none of which exist on Linux, so those
# files are excluded from the auto-globbed source list entirely on
# non-Windows platforms (an explicit SOURCES list, per engine_add_module's
# own documented escape hatch for "sources differ per OS", rather than
# wrapping nearly an entire file's contents in #if defined(_WIN32)).
file(GLOB_RECURSE RHI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
)
if(NOT WIN32)
    list(FILTER RHI_SOURCES EXCLUDE REGEX "/Private/D3D12/")
endif()

engine_add_module(
    NAME RHI
    TYPE STATIC
    LANGUAGE C CXX
    SOURCES ${RHI_SOURCES}
    PUBLIC_DEPENDENCIES
        Fluxion::Foundation
        Fluxion::Application
    PRIVATE_DEPENDENCIES
        Fluxion::Platform
)

# Vulkan::Vulkan/vma/OpenGL::GL/glheaders are all PRIVATE: the public RHI
# headers never mention a Vulkan, VMA, or OpenGL type (Rule 26/28 in
# ARCHITECTURE_RULES.md -- backend-native graphics types never leak into
# the portable API), so nothing outside Private/Vulkan or Private/OpenGL
# needs these include paths or link inputs.
target_link_libraries(RHI PRIVATE Vulkan::Vulkan vma OpenGL::GL glheaders)

if(UNIX AND NOT APPLE)
    # The GLX path in Private/OpenGL needs X11/Xlib.h + libX11 -- already a
    # transitive dependency of Application's own Xlib backend, linked here
    # explicitly too so RHI doesn't rely on that being incidentally true.
    find_package(X11 REQUIRED)
    target_include_directories(RHI PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(RHI PRIVATE ${X11_LIBRARIES})
endif()

if(WIN32)
    # d3d12/dxgi are the OS-supplied import libs (declarations come from
    # the Windows SDK, already on MSVC's default include path -- no
    # find_package/vendored-header step needed, unlike Vulkan's SDK or
    # OpenGL's missing GL1.2+/WGL-extension headers).
    target_link_libraries(RHI PRIVATE d3d12 dxgi d3d12ma)
endif()
