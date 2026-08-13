# Private/Events/*.c, Private/Input/*.c and Private/Time/*.c are
# platform-independent (Foundation-backed logic over a counter Platform
# already offers everywhere) and always compiled in, alongside whichever
# OS backend applies.
file(GLOB APPLICATION_COMMON_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/Events/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/Input/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/Time/*.c"
)

set(APPLICATION_SOURCES ${APPLICATION_COMMON_SOURCES})

if(WIN32)
    file(GLOB APPLICATION_PLATFORM_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Private/Windows/*.c")
elseif(UNIX)
    file(GLOB APPLICATION_PLATFORM_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Private/Linux/*.c")
else()
    message(FATAL_ERROR "Fluxion Application module: no backend for this host (only Windows/Linux exist so far)")
endif()

list(APPEND APPLICATION_SOURCES ${APPLICATION_PLATFORM_SOURCES})

engine_add_module(
    NAME Application
    TYPE STATIC
    LANGUAGE C CXX
    SOURCES ${APPLICATION_SOURCES}
    PUBLIC_DEPENDENCIES
        Fluxion::Foundation
    PRIVATE_DEPENDENCIES
        # Private/Time/Time.c only -- the counter it builds on is the
        # platform's, but no public header of this module exposes it, so
        # nothing that links Application has to know Platform exists.
        Fluxion::Platform
)

if(WIN32)
    # GetDpiForMonitor (Display_Windows.c) lives in Shcore, and
    # XInputGetState (Input_Windows.c) in Xinput — neither is part of the
    # default link set.
    target_link_libraries(Application PRIVATE Shcore Xinput)
elseif(UNIX)
    find_package(X11 REQUIRED)
    if(NOT X11_Xrandr_FOUND)
        message(FATAL_ERROR "Fluxion Application module needs the Xrandr X11 extension (libxrandr-dev)")
    endif()
    target_include_directories(Application PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(Application PRIVATE ${X11_LIBRARIES} ${X11_Xrandr_LIB})
endif()
