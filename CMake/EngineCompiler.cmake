# The contents of this file are subject to the Common Public Attribution
# License Version 1.0 (the "License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# https://opensource.org/license/cpal-1-0. The License is based on the
# Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
# to cover use of software over a computer network and provide for limited
# attribution for the Original Developer. In addition, Exhibit A has been
# modified to be consistent with Exhibit B.
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is Fluxion Engine.
#
# The Original Developer is not the Initial Developer and is __________. If
# left blank, the Original Developer is the Initial Developer.
#
# The Initial Developer of the Original Code is Kiss Tibor Péter. All
# portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
# All Rights Reserved.
#
# Contributor ______________________.
#
# Alternatively, the contents of this file may be used under the terms of
# the Fluxion Engine Commercial License Agreement Version 1.0, separately
# obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
# License"), in which case the provisions of the Commercial License are
# applicable instead of those above.
#
# If you wish to allow use of your version of this file only under the terms
# of the Commercial License and not to allow others to use your version of
# this file under the CPAL, indicate your decision by deleting the
# provisions above and replace them with the notice and other provisions
# required by the Commercial License. If you do not delete the provisions
# above, a recipient may use your version of this file under either the CPAL
# or the Commercial License.
#
# SPDX-License-Identifier: CPAL-1.0

include_guard(GLOBAL)

# Language standards: C23/C++23 project-wide.
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Every static module library (Foundation, Platform, Core, Application)
# can end up linked into a SHARED plugin -- without this, GCC/Clang only
# happen to get away with it because normal (non-unity) archive linking
# pulls in just the specific .o members a plugin actually references; a
# unity build collapses each module into a single .o, so as soon as that
# one object contains anything not compiled -fPIC, linking any shared
# plugin against it fails with a relocation error. A no-op on MSVC.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    set(ENGINE_COMPILER_MSVC ON)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(ENGINE_COMPILER_CLANG ON)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(ENGINE_COMPILER_GCC ON)
endif()

if(MSVC)
    # <stdatomic.h> in MSVC's C mode is gated behind this experimental
    # switch (as of this MSVC toolset); C++ <atomic> is unaffected and
    # does not need it, hence scoping to the C compile language only.
    add_compile_options($<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>)
endif()

option(FLUXION_ENABLE_ASAN "Enable AddressSanitizer (and UndefinedBehaviorSanitizer where supported)" OFF)

if(FLUXION_ENABLE_ASAN)
    if(MSVC)
        add_compile_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address,undefined)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# ThreadSanitizer -- MSVC has no /fsanitize=thread, so this is a
# Linux/GCC-only leg. Can't combine with ASan (the two instrumentations
# are mutually incompatible), so both ON is a configure-time error rather
# than one silently overriding the other.
option(FLUXION_ENABLE_TSAN "Enable ThreadSanitizer (Linux/GCC only)" OFF)

if(FLUXION_ENABLE_TSAN)
    if(FLUXION_ENABLE_ASAN)
        message(FATAL_ERROR "FLUXION_ENABLE_TSAN and FLUXION_ENABLE_ASAN cannot both be ON -- ASan and TSan instrumentation cannot be combined")
    endif()
    if(MSVC)
        message(FATAL_ERROR "FLUXION_ENABLE_TSAN is not supported by MSVC (no /fsanitize=thread) -- use the linux-gcc preset instead")
    endif()
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
endif()

# The whole engine (modules, tests, samples) must always build without
# unity build -- unity builds can mask a missing #include that a normal
# build would catch, so this stays OFF by default and is only turned on
# to spot-check that it still compiles, never as the everyday build mode.
# CMAKE_UNITY_BUILD is CMake's own global switch (applies uniformly to
# every target, unlike a per-target property that would miss test/sample
# executables built outside engine_add_module).
option(FLUXION_UNITY_BUILD "Build every target as a CMake unity/jumbo build" OFF)
set(CMAKE_UNITY_BUILD ${FLUXION_UNITY_BUILD})

# Off by default -- link-time optimization trades build time for runtime
# performance, so it's an opt-in release/verification build, not the
# everyday dev loop.
option(FLUXION_ENABLE_LTO "Enable link-time / interprocedural optimization" OFF)

if(FLUXION_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ENGINE_IPO_SUPPORTED OUTPUT ENGINE_IPO_ERROR)
    if(NOT ENGINE_IPO_SUPPORTED)
        message(FATAL_ERROR "FLUXION_ENABLE_LTO requested but IPO/LTO is not supported by this toolchain: ${ENGINE_IPO_ERROR}")
    endif()
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# Gates Foundation's memory domain/allocation tracking (MemoryTracker.h).
# Project-wide, not per-target: it's a public header behavior switch, so
# every translation unit that includes MemoryTracker.h needs to see the
# same value. When OFF, MemoryTracker.h's own API compiles to inline
# no-ops -- this define disables the tracking at compile time, not with a
# runtime branch.
option(FLUXION_MEMORY_TRACKING "Enable Foundation memory domain/allocation tracking" ON)
add_compile_definitions(FLUXION_MEMORY_TRACKING=$<BOOL:${FLUXION_MEMORY_TRACKING}>)

# Gates Core's CPU profiling hooks (Profiler.h). Same rationale as
# FLUXION_MEMORY_TRACKING above: project-wide because it's a public
# header behavior switch, and OFF compiles the whole Profiler API down
# to inline no-ops rather than a runtime branch.
option(FLUXION_PROFILING "Enable Core CPU profiling hooks" ON)
add_compile_definitions(FLUXION_PROFILING=$<BOOL:${FLUXION_PROFILING}>)

# engine_set_cpp_policy(<target> [NO_EXCEPTIONS] [NO_RTTI])
#
# Applies the Core/Runtime C++ policy (exceptions off, RTTI off) to a
# target's C++ translation units only -- flags are scoped to
# COMPILE_LANGUAGE:CXX so a target mixing C and C++ sources doesn't pass
# C++-only flags to its C compiles.
function(engine_set_cpp_policy TARGET_NAME)
    set(options NO_EXCEPTIONS NO_RTTI)
    cmake_parse_arguments(ENGINE_CPP_POLICY "${options}" "" "" ${ARGN})

    if(ENGINE_CPP_POLICY_NO_RTTI)
        if(MSVC)
            target_compile_options(${TARGET_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/GR->)
        else()
            target_compile_options(${TARGET_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
        endif()
    endif()

    if(ENGINE_CPP_POLICY_NO_EXCEPTIONS)
        if(MSVC)
            # No /EHsc is passed, and the MSVC STL is told not to rely on
            # exceptions internally -- together this makes a stray
            # try/throw/catch a hard compile error instead of a silently
            # tolerated construct.
            target_compile_definitions(${TARGET_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:_HAS_EXCEPTIONS=0>)
        else()
            target_compile_options(${TARGET_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
        endif()
    endif()
endfunction()
