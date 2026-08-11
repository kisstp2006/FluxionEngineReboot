include_guard(GLOBAL)

# engine_add_test(
#     NAME <name>
#     SOURCES <file>...
#     [DEPENDENCIES <target>...]
#     [NO_EXCEPTIONS]                    # disable C++ exceptions on this target's CXX sources
#     [NO_RTTI]                          # disable C++ RTTI on this target's CXX sources
#     [PCH <header>...]                  # optional: precompiled headers, private to this executable's own TUs
# )
#
# Builds a plain executable and registers it with CTest. No external test
# framework — the executable itself decides pass/fail via its exit code.
function(engine_add_test)
    set(options NO_EXCEPTIONS NO_RTTI)
    set(oneValueArgs NAME)
    set(multiValueArgs SOURCES DEPENDENCIES PCH)
    cmake_parse_arguments(ENGINE_TEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ENGINE_TEST_NAME)
        message(FATAL_ERROR "engine_add_test: NAME is required")
    endif()

    if(NOT ENGINE_TEST_SOURCES)
        message(FATAL_ERROR "engine_add_test(${ENGINE_TEST_NAME}): SOURCES is required")
    endif()

    add_executable(${ENGINE_TEST_NAME} ${ENGINE_TEST_SOURCES})
    engine_set_warnings(${ENGINE_TEST_NAME})

    if(ENGINE_TEST_DEPENDENCIES)
        target_link_libraries(${ENGINE_TEST_NAME} PRIVATE ${ENGINE_TEST_DEPENDENCIES})
    endif()

    if(ENGINE_TEST_NO_EXCEPTIONS OR ENGINE_TEST_NO_RTTI)
        set(ENGINE_TEST_CPP_POLICY_ARGS)
        if(ENGINE_TEST_NO_EXCEPTIONS)
            list(APPEND ENGINE_TEST_CPP_POLICY_ARGS NO_EXCEPTIONS)
        endif()
        if(ENGINE_TEST_NO_RTTI)
            list(APPEND ENGINE_TEST_CPP_POLICY_ARGS NO_RTTI)
        endif()
        engine_set_cpp_policy(${ENGINE_TEST_NAME} ${ENGINE_TEST_CPP_POLICY_ARGS})
    endif()

    if(ENGINE_TEST_PCH)
        # Scoped to CXX TUs only (same reasoning as engine_add_module's
        # PCH handling) -- most test executables mix .c and .cpp sources.
        set(ENGINE_TEST_PCH_SCOPED)
        foreach(ENGINE_TEST_PCH_HEADER ${ENGINE_TEST_PCH})
            if(ENGINE_TEST_PCH_HEADER MATCHES "^<(.*)>$")
                # See EngineModule.cmake's PCH handling for why $<ANGLE-R>
                # is needed here instead of a literal closing bracket.
                list(APPEND ENGINE_TEST_PCH_SCOPED "$<$<COMPILE_LANGUAGE:CXX>:<${CMAKE_MATCH_1}$<ANGLE-R>>")
            else()
                list(APPEND ENGINE_TEST_PCH_SCOPED "$<$<COMPILE_LANGUAGE:CXX>:${ENGINE_TEST_PCH_HEADER}>")
            endif()
        endforeach()
        target_precompile_headers(${ENGINE_TEST_NAME} PRIVATE ${ENGINE_TEST_PCH_SCOPED})
    endif()

    add_test(NAME ${ENGINE_TEST_NAME} COMMAND ${ENGINE_TEST_NAME})
endfunction()
