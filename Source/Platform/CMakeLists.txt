set(PLATFORM_SOURCES "")

if(WIN32)
    file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Private/Windows/*.c")
elseif(UNIX)
    file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Private/Linux/*.c")
else()
    message(FATAL_ERROR "Fluxion Platform module: no backend for this host (only Windows/Linux exist so far)")
endif()

engine_add_module(
    NAME Platform
    TYPE STATIC
    LANGUAGE C CXX
    SOURCES ${PLATFORM_SOURCES}
    PUBLIC_DEPENDENCIES
        Fluxion::Foundation
)

if(UNIX)
    # CMAKE_C_EXTENSIONS is OFF project-wide, so GCC/Clang compile with
    # -std=c2x (strict ISO), which hides POSIX/glibc declarations
    # (CLOCK_MONOTONIC, clock_gettime, nanosleep, readlink,
    # pthread_setname_np, ...) behind feature-test macros. _GNU_SOURCE
    # covers all of that in one shot for the whole Linux backend.
    target_compile_definitions(Platform PRIVATE _GNU_SOURCE)
    target_link_libraries(Platform PRIVATE ${CMAKE_DL_LIBS} pthread)
endif()
