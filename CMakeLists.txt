# Visual Studio 2022 17.10+/2026 auto-imports vcpkg.props/vcpkg.targets
# into every vcxproj with VcpkgEnabled defaulting to true, regardless of
# whether the project asked for vcpkg -- on a machine with an unrelated
# project's vcpkg install (e.g. found via VS's own auto-detection), this
# silently substitutes THAT vcpkg's headers/libs/DLLs for ours (observed:
# a mismatched vulkan_win32.h and a broken vcpkg-built vulkan-1.dll got
# picked up ahead of the LunarG SDK). Fluxion vendors its own third-party
# code (ThirdParty/) and finds the Vulkan SDK via find_package(Vulkan) --
# it never wants vcpkg, so this is switched off project-wide before
# project() generates any vcxproj.
set(CMAKE_VS_GLOBALS "VcpkgEnabled=false" CACHE STRING "" FORCE)

cmake_minimum_required(VERSION 3.28)

project(FluxionEngine
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")
include(Engine)

option(FLUXION_BUILD_TESTS "Build Fluxion Engine tests" ON)
option(FLUXION_BUILD_SAMPLES "Build Fluxion Engine sample programs" ON)

if(FLUXION_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(ThirdParty/pdjson)
add_subdirectory(ThirdParty/vma)

add_subdirectory(Source/Foundation)
add_subdirectory(Source/Platform)
add_subdirectory(Source/Core)
add_subdirectory(Source/Application)
add_subdirectory(Source/RHI)

if(FLUXION_BUILD_TESTS)
    add_subdirectory(Tests/FoundationTests)
    add_subdirectory(Tests/PlatformTests)
    add_subdirectory(Tests/CoreTests)
    add_subdirectory(Tests/ApplicationTests)
    add_subdirectory(Tests/RHITests)
endif()

if(FLUXION_BUILD_SAMPLES)
    add_subdirectory(Samples/InputDemo)
    add_subdirectory(Samples/VulkanTriangleDemo)
endif()
