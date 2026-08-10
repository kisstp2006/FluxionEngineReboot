cmake_minimum_required(VERSION 3.28)

project(FluxionEngine
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")
include(Engine)

option(FLUXION_BUILD_TESTS "Build Fluxion Engine tests" ON)

if(FLUXION_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(Source/Foundation)

if(FLUXION_BUILD_TESTS)
    add_subdirectory(Tests/FoundationTests)
endif()
