include_guard(GLOBAL)

# Which operating system is being built for, and what that means for the
# modules that have one directory of sources per OS.
#
# Worked out once, here, rather than by each module asking CMake's own
# variables again. Those variables are easy to get subtly wrong: ANDROID
# is also UNIX, so a module written as `if(WIN32) ... elseif(UNIX)` picks
# the desktop Linux backend for an Android build and then fails somewhere
# far away, in the link, against an X11 symbol nobody asked for. Deciding
# it in one place means a new OS is added by editing this file, and every
# module follows.
#
# FLUXION_PLATFORM_DIR names the subdirectory of a module's Private/ tree
# that holds this OS's backend. It is empty for an OS the engine knows
# nothing about, which is not the same as "this module has no OS-specific
# part" -- the modules that need one say so themselves, by name, so the
# message is about a missing backend rather than about a glob that came
# back empty.

if(ANDROID)
    set(FLUXION_PLATFORM_NAME "Android")
    set(FLUXION_PLATFORM_DIR "Android")
elseif(WIN32)
    set(FLUXION_PLATFORM_NAME "Windows")
    set(FLUXION_PLATFORM_DIR "Windows")
elseif(UNIX AND NOT APPLE)
    set(FLUXION_PLATFORM_NAME "Linux")
    set(FLUXION_PLATFORM_DIR "Linux")
else()
    set(FLUXION_PLATFORM_NAME "${CMAKE_SYSTEM_NAME}")
    set(FLUXION_PLATFORM_DIR "")
endif()

# Whether this OS has a desktop window system to talk to. Both the window
# and input backend and the renderer's surface creation hang off this: on
# Android a surface comes from the OS as the application starts, and there
# is no X server, no GLX, and nothing to find with find_package(X11).
if(FLUXION_PLATFORM_NAME STREQUAL "Windows" OR FLUXION_PLATFORM_NAME STREQUAL "Linux")
    set(FLUXION_HAS_DESKTOP_WINDOWING TRUE)
else()
    set(FLUXION_HAS_DESKTOP_WINDOWING FALSE)
endif()

# Whether the desktop OpenGL backend can be built. It is desktop GL
# through WGL or GLX, not GL ES through EGL, so it belongs to the same
# question as the one above.
set(FLUXION_HAS_DESKTOP_OPENGL ${FLUXION_HAS_DESKTOP_WINDOWING})

# Collects one module's OS-specific sources, or fails with a message
# naming the module and the OS. Call it where a module needs a backend it
# cannot do without; a module with no OS-specific part simply does not
# call it.
#
#   engine_platform_sources(<out-var> <module-name> [REQUIRED] [FALLBACK <dir>...])
#
# This OS's own directory is looked for first. FALLBACK names other
# backends THIS MODULE would be satisfied by, in order, and is how a
# module says that its OS-specific part is not really specific to one OS
# -- that it is a set of interfaces more than one of them provides.
#
# It is per module and not per OS because the same two operating systems
# can be alike in one module and unlike in another. Android and Linux
# share a kernel and a set of system calls, so a module whose backend is
# nothing but those can use one directory for both; the module that opens
# an X display cannot, and must not be handed the Linux sources just
# because Android is also UNIX.
#
# The first directory that exists wins outright -- the search stops there
# rather than merging two of them, because a half-overridden backend is
# harder to follow than a whole one.
#
# Without REQUIRED, finding nothing yields an empty list and the build
# carries on, which is what a module wants when its OS-specific part is
# optional.
function(engine_platform_sources outVar moduleName)
    cmake_parse_arguments(ARG "REQUIRED" "" "FALLBACK" ${ARGN})

    set(candidates "")
    if(NOT FLUXION_PLATFORM_DIR STREQUAL "")
        list(APPEND candidates "${FLUXION_PLATFORM_DIR}")
    endif()
    list(APPEND candidates ${ARG_FALLBACK})

    foreach(candidate ${candidates})
        set(backendDir "${CMAKE_CURRENT_SOURCE_DIR}/Private/${candidate}")
        if(IS_DIRECTORY "${backendDir}")
            file(GLOB backendSources CONFIGURE_DEPENDS "${backendDir}/*.c" "${backendDir}/*.cpp")
            set(${outVar} ${backendSources} PARENT_SCOPE)
            return()
        endif()
    endforeach()

    if(ARG_REQUIRED)
        message(FATAL_ERROR
            "Fluxion ${moduleName} module: nothing to build for ${FLUXION_PLATFORM_NAME}. "
            "Looked under Source/${moduleName}/Private/ for: ${candidates}. "
            "Write Source/${moduleName}/Private/${FLUXION_PLATFORM_DIR}/.")
    endif()

    set(${outVar} "" PARENT_SCOPE)
endfunction()
