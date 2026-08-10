include_guard(GLOBAL)

# engine_add_module(
#     NAME <name>
#     [TYPE STATIC|SHARED|INTERFACE]     # default STATIC
#     [LANGUAGE C CXX]                   # informational, for future per-module language gating
#     [PUBLIC_DEPENDENCIES <target>...]
#     [PRIVATE_DEPENDENCIES <target>...]
# )
#
# Expects the calling CMakeLists.txt to live in a module directory with the
# Include/ (public headers) and Private/ (implementation) layout described
# in plans/Architecture_Plan_Part1.md section 11. Creates both `<name>` and
# an `Fluxion::<name>` ALIAS target.
function(engine_add_module)
    set(options)
    set(oneValueArgs NAME TYPE)
    set(multiValueArgs LANGUAGE PUBLIC_DEPENDENCIES PRIVATE_DEPENDENCIES)
    cmake_parse_arguments(ENGINE_MODULE "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ENGINE_MODULE_NAME)
        message(FATAL_ERROR "engine_add_module: NAME is required")
    endif()

    if(NOT ENGINE_MODULE_TYPE)
        set(ENGINE_MODULE_TYPE STATIC)
    endif()

    set(ENGINE_MODULE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Include")
    set(ENGINE_MODULE_PRIVATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Private")

    if(ENGINE_MODULE_TYPE STREQUAL "INTERFACE")
        add_library(${ENGINE_MODULE_NAME} INTERFACE)
        target_include_directories(${ENGINE_MODULE_NAME} INTERFACE "${ENGINE_MODULE_INCLUDE_DIR}")
    else()
        file(GLOB_RECURSE ENGINE_MODULE_PUBLIC_HEADERS CONFIGURE_DEPENDS
            "${ENGINE_MODULE_INCLUDE_DIR}/*.h" "${ENGINE_MODULE_INCLUDE_DIR}/*.hpp")
        file(GLOB_RECURSE ENGINE_MODULE_PRIVATE_SOURCES CONFIGURE_DEPENDS
            "${ENGINE_MODULE_PRIVATE_DIR}/*.c" "${ENGINE_MODULE_PRIVATE_DIR}/*.cpp")
        file(GLOB_RECURSE ENGINE_MODULE_PRIVATE_HEADERS CONFIGURE_DEPENDS
            "${ENGINE_MODULE_PRIVATE_DIR}/*.h" "${ENGINE_MODULE_PRIVATE_DIR}/*.hpp")

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
    endif()

    add_library(Fluxion::${ENGINE_MODULE_NAME} ALIAS ${ENGINE_MODULE_NAME})

    if(ENGINE_MODULE_PUBLIC_DEPENDENCIES)
        target_link_libraries(${ENGINE_MODULE_NAME} PUBLIC ${ENGINE_MODULE_PUBLIC_DEPENDENCIES})
    endif()

    if(ENGINE_MODULE_PRIVATE_DEPENDENCIES)
        target_link_libraries(${ENGINE_MODULE_NAME} PRIVATE ${ENGINE_MODULE_PRIVATE_DEPENDENCIES})
    endif()
endfunction()
