# Every function this module declares is an OS service, so without a
# backend there is nothing to build at all -- hence REQUIRED.
#
# Android falls back to the Linux backend because that is what it is:
# threads, timing, files, dynamic loading and the two reads of /proc are
# the same system calls there, and its C library declares them the same
# way. Compiling the Linux sources with the Android NDK toolchain for
# aarch64 was checked and produces no diagnostic at the warning level
# this project builds at. Whether each of them BEHAVES the same on a
# device is a separate question, and one that only a device answers.
engine_platform_sources(PLATFORM_SOURCES Platform REQUIRED FALLBACK Linux)

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

    # Android's C library has the threading and dynamic-loading entry
    # points built in rather than in libpthread/libdl, and asking for
    # those two by name there fails to find them.
    if(NOT ANDROID)
        target_link_libraries(Platform PRIVATE ${CMAKE_DL_LIBS} pthread)
    endif()
endif()
