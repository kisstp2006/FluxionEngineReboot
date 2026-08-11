include_guard(GLOBAL)

# engine_add_module(
#     NAME <name>
#     [TYPE STATIC|SHARED|INTERFACE]     # default STATIC
#     [LANGUAGE C CXX]                   # default both (Mixed); C or CXX alone narrows what gets auto-globbed/compiled
#     [SOURCES <file>...]                # optional: use this exact list instead of auto-globbing Private/**/*.c(pp)
#     [NO_EXCEPTIONS]                    # disable C++ exceptions on this target's CXX sources
#     [NO_RTTI]                          # disable C++ RTTI on this target's CXX sources
#     [PCH <header>...]                  # optional: precompiled headers, private to this target's own TUs only
#     [PUBLIC_DEPENDENCIES <target>...]
#     [PRIVATE_DEPENDENCIES <target>...]
# )
#
# Expects the calling CMakeLists.txt to live in a module directory with an
# Include/ (public headers) and Private/ (implementation) layout. Creates
# both `<name>` and an `Fluxion::<name>` ALIAS target.
#
# By default, Private/**/*.c(pp) is auto-globbed. Pass SOURCES explicitly
# when a module needs to select sources itself (e.g. Platform choosing
# Private/Windows/*.c vs Private/Linux/*.c based on the host OS) — headers
# are still auto-globbed from Include/ either way, only for IDE listing.
#
# LANGUAGE narrows auto-globbing (ignored when SOURCES is given explicitly,
# since the caller already picked exact files): `LANGUAGE C` only globs
# Private/**/*.c (no .cpp) and never applies engine_set_cpp_policy, since
# NO_EXCEPTIONS/NO_RTTI are C++-only concerns. `LANGUAGE CXX` only globs
# Private/**/*.cpp (no .c) but still sees both `.h` and `.hpp` public
# headers, since C++ can freely include a C-compatible header. The default
# (both, or LANGUAGE not given) is today's Mixed behavior: `.h`+`.hpp`
# headers, `.c`+`.cpp` sources, policy applied if requested.
function(engine_add_module)
    set(options NO_EXCEPTIONS NO_RTTI)
    set(oneValueArgs NAME TYPE)
    set(multiValueArgs LANGUAGE SOURCES PCH PUBLIC_DEPENDENCIES PRIVATE_DEPENDENCIES)
    cmake_parse_arguments(ENGINE_MODULE "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ENGINE_MODULE_NAME)
        message(FATAL_ERROR "engine_add_module: NAME is required")
    endif()

    if(NOT ENGINE_MODULE_TYPE)
        set(ENGINE_MODULE_TYPE STATIC)
    endif()

    set(ENGINE_MODULE_IS_C_ONLY FALSE)
    set(ENGINE_MODULE_IS_CXX_ONLY FALSE)
    list(LENGTH ENGINE_MODULE_LANGUAGE ENGINE_MODULE_LANGUAGE_COUNT)
    if(ENGINE_MODULE_LANGUAGE_COUNT EQUAL 1)
        if(ENGINE_MODULE_LANGUAGE STREQUAL "C")
            set(ENGINE_MODULE_IS_C_ONLY TRUE)
        elseif(ENGINE_MODULE_LANGUAGE STREQUAL "CXX")
            set(ENGINE_MODULE_IS_CXX_ONLY TRUE)
        else()
            message(FATAL_ERROR "engine_add_module(${ENGINE_MODULE_NAME}): LANGUAGE must be C, CXX, or C CXX")
        endif()
    endif()

    if(ENGINE_MODULE_IS_C_ONLY AND (ENGINE_MODULE_NO_EXCEPTIONS OR ENGINE_MODULE_NO_RTTI))
        message(FATAL_ERROR "engine_add_module(${ENGINE_MODULE_NAME}): NO_EXCEPTIONS/NO_RTTI apply only to C++ (LANGUAGE CXX or C CXX)")
    endif()

    set(ENGINE_MODULE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Include")
    set(ENGINE_MODULE_PRIVATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Private")

    if(ENGINE_MODULE_TYPE STREQUAL "INTERFACE")
        add_library(${ENGINE_MODULE_NAME} INTERFACE)
        target_include_directories(${ENGINE_MODULE_NAME} INTERFACE "${ENGINE_MODULE_INCLUDE_DIR}")
    else()
        if(ENGINE_MODULE_IS_C_ONLY)
            file(GLOB_RECURSE ENGINE_MODULE_PUBLIC_HEADERS CONFIGURE_DEPENDS "${ENGINE_MODULE_INCLUDE_DIR}/*.h")
            file(GLOB_RECURSE ENGINE_MODULE_PRIVATE_HEADERS CONFIGURE_DEPENDS "${ENGINE_MODULE_PRIVATE_DIR}/*.h")
        else()
            file(GLOB_RECURSE ENGINE_MODULE_PUBLIC_HEADERS CONFIGURE_DEPENDS
                "${ENGINE_MODULE_INCLUDE_DIR}/*.h" "${ENGINE_MODULE_INCLUDE_DIR}/*.hpp")
            file(GLOB_RECURSE ENGINE_MODULE_PRIVATE_HEADERS CONFIGURE_DEPENDS
                "${ENGINE_MODULE_PRIVATE_DIR}/*.h" "${ENGINE_MODULE_PRIVATE_DIR}/*.hpp")
        endif()

        if(ENGINE_MODULE_SOURCES)
            set(ENGINE_MODULE_PRIVATE_SOURCES ${ENGINE_MODULE_SOURCES})
        elseif(ENGINE_MODULE_IS_C_ONLY)
            file(GLOB_RECURSE ENGINE_MODULE_PRIVATE_SOURCES CONFIGURE_DEPENDS "${ENGINE_MODULE_PRIVATE_DIR}/*.c")
        elseif(ENGINE_MODULE_IS_CXX_ONLY)
            file(GLOB_RECURSE ENGINE_MODULE_PRIVATE_SOURCES CONFIGURE_DEPENDS "${ENGINE_MODULE_PRIVATE_DIR}/*.cpp")
        else()
            file(GLOB_RECURSE ENGINE_MODULE_PRIVATE_SOURCES CONFIGURE_DEPENDS
                "${ENGINE_MODULE_PRIVATE_DIR}/*.c" "${ENGINE_MODULE_PRIVATE_DIR}/*.cpp")
        endif()

        if(NOT ENGINE_MODULE_PRIVATE_SOURCES)
            message(FATAL_ERROR "engine_add_module(${ENGINE_MODULE_NAME}): no .c/.cpp sources found under ${ENGINE_MODULE_PRIVATE_DIR}")
        endif()

        add_library(${ENGINE_MODULE_NAME} ${ENGINE_MODULE_TYPE}
            ${ENGINE_MODULE_PUBLIC_HEADERS}
            ${ENGINE_MODULE_PRIVATE_SOURCES}
            ${ENGINE_MODULE_PRIVATE_HEADERS}
        )

        target_include_directories(${ENGINE_MODULE_NAME}
            PUBLIC  "${ENGINE_MODULE_INCLUDE_DIR}"
            PRIVATE "${ENGINE_MODULE_PRIVATE_DIR}"
        )

        engine_set_warnings(${ENGINE_MODULE_NAME})

        if(ENGINE_MODULE_NO_EXCEPTIONS OR ENGINE_MODULE_NO_RTTI)
            set(ENGINE_MODULE_CPP_POLICY_ARGS)
            if(ENGINE_MODULE_NO_EXCEPTIONS)
                list(APPEND ENGINE_MODULE_CPP_POLICY_ARGS NO_EXCEPTIONS)
            endif()
            if(ENGINE_MODULE_NO_RTTI)
                list(APPEND ENGINE_MODULE_CPP_POLICY_ARGS NO_RTTI)
            endif()
            engine_set_cpp_policy(${ENGINE_MODULE_NAME} ${ENGINE_MODULE_CPP_POLICY_ARGS})
        endif()

        if(ENGINE_MODULE_PCH)
            # PRIVATE only -- a consumer of Fluxion::<name> (another
            # module, a test executable) must never have this PCH forced
            # onto it, and no public header of this module may assume the
            # PCH's contents are ambiently visible (public headers are
            # compiled standalone by engine_add_header_selfcontained_test
            # without any PCH involved, so this is structurally enforced).
            # Each header is scoped to CXX TUs only, same generator
            # expression engine_set_cpp_policy already uses -- a target
            # mixing C and C++ sources must not try to precompile a C++
            # standard header for its C compiles too.
            set(ENGINE_MODULE_PCH_SCOPED)
            foreach(ENGINE_MODULE_PCH_HEADER ${ENGINE_MODULE_PCH})
                if(ENGINE_MODULE_PCH_HEADER MATCHES "^<(.*)>$")
                    # A literal `<header>` can't be nested directly inside
                    # the COMPILE_LANGUAGE genex below -- both use `<`/`>`
                    # as their own delimiters. $<ANGLE-R> is CMake's
                    # documented escape for the closing bracket in exactly
                    # this situation.
                    list(APPEND ENGINE_MODULE_PCH_SCOPED "$<$<COMPILE_LANGUAGE:CXX>:<${CMAKE_MATCH_1}$<ANGLE-R>>")
                else()
                    list(APPEND ENGINE_MODULE_PCH_SCOPED "$<$<COMPILE_LANGUAGE:CXX>:${ENGINE_MODULE_PCH_HEADER}>")
                endif()
            endforeach()
            target_precompile_headers(${ENGINE_MODULE_NAME} PRIVATE ${ENGINE_MODULE_PCH_SCOPED})
        endif()
    endif()

    add_library(Fluxion::${ENGINE_MODULE_NAME} ALIAS ${ENGINE_MODULE_NAME})

    if(ENGINE_MODULE_PUBLIC_DEPENDENCIES)
        target_link_libraries(${ENGINE_MODULE_NAME} PUBLIC ${ENGINE_MODULE_PUBLIC_DEPENDENCIES})
    endif()

    if(ENGINE_MODULE_PRIVATE_DEPENDENCIES)
        target_link_libraries(${ENGINE_MODULE_NAME} PRIVATE ${ENGINE_MODULE_PRIVATE_DEPENDENCIES})
    endif()
endfunction()
