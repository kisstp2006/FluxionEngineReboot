# Which backends this build contains.
#
# Vulkan is the one that runs everywhere the engine targets, so it is the
# baseline. The other two are tied to something the target either has or
# does not: D3D12 is a Windows API, and Private/OpenGL is DESKTOP OpenGL
# reached through WGL or GLX -- not GL ES through EGL, which is a
# different set of entry points and a different surface, and would be its
# own backend rather than a flag on this one.
set(FLUXION_RHI_BACKEND_VULKAN ON)

if(WIN32)
    set(FLUXION_RHI_BACKEND_D3D12 ON)
else()
    set(FLUXION_RHI_BACKEND_D3D12 OFF)
endif()

if(FLUXION_HAS_DESKTOP_OPENGL)
    set(FLUXION_RHI_BACKEND_OPENGL ON)
else()
    set(FLUXION_RHI_BACKEND_OPENGL OFF)
endif()

find_package(Vulkan REQUIRED)
if(FLUXION_RHI_BACKEND_OPENGL)
    find_package(OpenGL REQUIRED)
endif()

# An explicit source list rather than the module's own auto-glob, per
# engine_add_module's documented escape hatch for "sources differ per OS":
# a backend that is off must not be compiled at all, because each one
# links against libraries the target may not have.
file(GLOB_RECURSE RHI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
)
if(NOT FLUXION_RHI_BACKEND_D3D12)
    list(FILTER RHI_SOURCES EXCLUDE REGEX "/Private/D3D12/")
endif()
if(NOT FLUXION_RHI_BACKEND_OPENGL)
    list(FILTER RHI_SOURCES EXCLUDE REGEX "/Private/OpenGL/")
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

# What Private/RHI.c compiles its backend table from. Kept PRIVATE: a
# caller asks Fluxion_RHI_IsBackendAvailable at runtime rather than
# reading the build's own configuration out of a macro, so that a program
# built once against this library cannot disagree with it.
foreach(backend VULKAN OPENGL D3D12)
    if(FLUXION_RHI_BACKEND_${backend})
        target_compile_definitions(RHI PRIVATE FLUXION_RHI_HAS_${backend}=1)
    else()
        target_compile_definitions(RHI PRIVATE FLUXION_RHI_HAS_${backend}=0)
    endif()
endforeach()

# Vulkan::Vulkan/vma/OpenGL::GL/glheaders are all PRIVATE: backend-native
# graphics types never leak into the portable API, so the public RHI
# headers never mention a Vulkan, VMA, or OpenGL type, and nothing outside
# Private/Vulkan or Private/OpenGL needs these include paths or link
# inputs.
target_link_libraries(RHI PRIVATE Vulkan::Vulkan vma)

if(FLUXION_RHI_BACKEND_OPENGL)
    target_link_libraries(RHI PRIVATE OpenGL::GL glheaders)
endif()

if(FLUXION_RHI_BACKEND_OPENGL AND FLUXION_PLATFORM_NAME STREQUAL "Linux")
    # The GLX path in Private/OpenGL needs X11/Xlib.h + libX11 -- already a
    # transitive dependency of Application's own Xlib backend, linked here
    # explicitly too so RHI doesn't rely on that being incidentally true.
    find_package(X11 REQUIRED)
    target_include_directories(RHI PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(RHI PRIVATE ${X11_LIBRARIES})
endif()

if(FLUXION_RHI_BACKEND_D3D12)
    # d3d12/dxgi are the OS-supplied import libs (declarations come from
    # the Windows SDK, already on MSVC's default include path -- no
    # find_package/vendored-header step needed, unlike Vulkan's SDK or
    # OpenGL's missing GL1.2+/WGL-extension headers).
    target_link_libraries(RHI PRIVATE d3d12 dxgi d3d12ma)
endif()
