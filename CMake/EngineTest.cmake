include_guard(GLOBAL)

# engine_add_test(
#     NAME <name>
#     SOURCES <file>...
#     [DEPENDENCIES <target>...]
# )
#
# Builds a plain executable and registers it with CTest. No external test
# framework — the executable itself decides pass/fail via its exit code.
function(engine_add_test)
    set(options)
    set(oneValueArgs NAME)
    set(multiValueArgs SOURCES DEPENDENCIES)
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

    add_test(NAME ${ENGINE_TEST_NAME} COMMAND ${ENGINE_TEST_NAME})
endfunction()
