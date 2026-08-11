include_guard(GLOBAL)

# Language standards: C23/C++23 project-wide.
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
