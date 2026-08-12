# Khronos GL/KHR extension headers (glcorearb.h, khrplatform.h, wglext.h,
# glxext.h) -- vendored, unmodified, Khronos Free Use License (MIT-
# equivalent; see ARCHITECTURE_RULES.md dependency policy). Third-party
# code: not run through engine_set_warnings or forced onto our C23/C++23
# setting.
#
# Unlike Vulkan (where the full SDK is located via find_package(Vulkan)),
# there is no equivalent OpenGL SDK to find -- the base GL 1.1 entry
# points and context-creation API come from the platform's own
# opengl32.lib/libGL (via find_package(OpenGL) in Source/RHI), but the
# GL 1.2+ function/enum declarations and the WGL/GLX extension surface
# are not shipped by the OS, so those specific headers are vendored here.
# GL/glx.h itself (base GLX, not the extension header) is NOT vendored --
# it is expected to come from the system's X11/libGL-dev headers, which
# the engine already requires for the Application module's Xlib backend.
add_library(glheaders INTERFACE)
target_include_directories(glheaders INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
