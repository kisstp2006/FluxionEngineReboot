include_guard(GLOBAL)

# Language standards. C23/C++23 are the project-wide target (see
# plans/Architecture_Plan_Part1.md, section 7, in the root — not in this
# repo). If a toolchain rejects a specific C23 feature during
# implementation, the fallback is documented in the root ROADMAP.md, not
# here.
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
