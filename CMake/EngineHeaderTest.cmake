include_guard(GLOBAL)

# engine_add_header_selfcontained_test(
#     MODULE <name>              # existing module target (Fluxion::<name> alias must exist)
#     HEADERS <include-path>...  # e.g. Fluxion/Core/Startup/Subsystem.hpp
# )
#
# For every header, generates a standalone .cpp translation unit whose only
# content is `#include <that-header>` -- proving the header compiles with no
# reliance on include order or on some other header having pulled in its
# dependencies first. All generated TUs, plus one shared main(), link into a
# single `<Module>HeaderSelfContainedTests` executable registered with CTest.
function(engine_add_header_selfcontained_test)
    set(options)
    set(oneValueArgs MODULE)
    set(multiValueArgs HEADERS)
    cmake_parse_arguments(ENGINE_HEADER_TEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ENGINE_HEADER_TEST_MODULE)
        message(FATAL_ERROR "engine_add_header_selfcontained_test: MODULE is required")
    endif()

    if(NOT ENGINE_HEADER_TEST_HEADERS)
        message(FATAL_ERROR "engine_add_header_selfcontained_test(${ENGINE_HEADER_TEST_MODULE}): HEADERS is required")
    endif()

    set(ENGINE_HEADER_TEST_NAME "${ENGINE_HEADER_TEST_MODULE}HeaderSelfContainedTests")
    set(ENGINE_HEADER_TEST_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/${ENGINE_HEADER_TEST_NAME}_Generated")

    set(ENGINE_HEADER_TEST_SOURCES)
    foreach(ENGINE_HEADER_TEST_HEADER ${ENGINE_HEADER_TEST_HEADERS})
        string(MAKE_C_IDENTIFIER "${ENGINE_HEADER_TEST_HEADER}" ENGINE_HEADER_TEST_ID)
        set(ENGINE_HEADER_TEST_TU "${ENGINE_HEADER_TEST_GENERATED_DIR}/${ENGINE_HEADER_TEST_ID}.cpp")
        file(GENERATE OUTPUT "${ENGINE_HEADER_TEST_TU}" CONTENT "#include <${ENGINE_HEADER_TEST_HEADER}>\n")
        list(APPEND ENGINE_HEADER_TEST_SOURCES "${ENGINE_HEADER_TEST_TU}")
    endforeach()

    set(ENGINE_HEADER_TEST_MAIN "${ENGINE_HEADER_TEST_GENERATED_DIR}/Main.cpp")
    file(GENERATE OUTPUT "${ENGINE_HEADER_TEST_MAIN}" CONTENT "int main() { return 0; }\n")
    list(APPEND ENGINE_HEADER_TEST_SOURCES "${ENGINE_HEADER_TEST_MAIN}")

    engine_add_test(
        NAME ${ENGINE_HEADER_TEST_NAME}
        SOURCES ${ENGINE_HEADER_TEST_SOURCES}
        DEPENDENCIES Fluxion::${ENGINE_HEADER_TEST_MODULE}
    )
endfunction()
