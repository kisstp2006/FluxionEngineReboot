# Building for a 64-bit ARM Linux machine from a different one.
#
# Everything here is about telling CMake it is NOT building for the
# machine it is running on. Without CMAKE_SYSTEM_NAME set, CMake treats
# the build as native and will happily try to run the things it compiles
# -- which is how a cross build ends up failing on a test program rather
# than on anything real.
#
# The compilers default to the Debian/Ubuntu cross toolchain names, which
# is what `apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu`
# installs. Pass -DFLUXION_AARCH64_TOOLCHAIN_PREFIX=... to name a
# different one; the prefix is used as-is, so it may be a bare name found
# on PATH or an absolute path.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED FLUXION_AARCH64_TOOLCHAIN_PREFIX)
    set(FLUXION_AARCH64_TOOLCHAIN_PREFIX "aarch64-linux-gnu-")
endif()

set(CMAKE_C_COMPILER   "${FLUXION_AARCH64_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${FLUXION_AARCH64_TOOLCHAIN_PREFIX}g++")

# Where to look for the target's own headers and libraries. The three
# rules below say: find programs on the HOST (the compiler itself, and any
# tool the build runs), but find libraries, headers and packages only
# under the target's root -- otherwise find_package(X11) would happily
# hand back the host's x86 libX11 and the link would fail with an
# architecture mismatch that reads as if the library were missing.
if(NOT DEFINED CMAKE_FIND_ROOT_PATH)
    set(CMAKE_FIND_ROOT_PATH "/usr/${FLUXION_AARCH64_TOOLCHAIN_PREFIX}")
    string(REGEX REPLACE "-$" "" CMAKE_FIND_ROOT_PATH "${CMAKE_FIND_ROOT_PATH}")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Setting CMAKE_SYSTEM_NAME above also sets CMAKE_CROSSCOMPILING, which
# is what keeps the tests from being registered with CTest here (see
# CMake/EngineTest.cmake): they are still built, because compiling them
# checks that the engine's headers and this toolchain agree, but running
# them belongs to whoever has the hardware.
