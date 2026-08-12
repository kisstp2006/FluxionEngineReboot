find_package(Vulkan REQUIRED)
find_package(OpenGL REQUIRED)

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
