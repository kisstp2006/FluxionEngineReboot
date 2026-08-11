# Private/Events/*.c and Private/Input/*.c are platform-independent (pure
# Foundation-backed logic) and always compiled in, alongside whichever OS
# backend applies.
file(GLOB APPLICATION_COMMON_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/Events/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/Input/*.c"
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
