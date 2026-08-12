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
add_subdirectory(ThirdParty/glheaders)
if(WIN32)
    # D3D12 (and therefore D3D12MA) is a Windows-only API -- this target
    # links d3d12.lib/dxgi.lib, which don't exist on Linux, so it's only
    # added to the build on Windows, same reasoning as Private/D3D12 only
    # being compiled into the RHI module there.
    add_subdirectory(ThirdParty/d3d12ma)
endif()

add_subdirectory(Source/Foundation)
add_subdirectory(Source/Platform)
add_subdirectory(Source/Core)
add_subdirectory(Source/Application)
add_subdirectory(Source/RHI)
add_subdirectory(Source/ShaderCompiler)

if(FLUXION_BUILD_TESTS)
    add_subdirectory(Tests/FoundationTests)
    add_subdirectory(Tests/PlatformTests)
    add_subdirectory(Tests/CoreTests)
    add_subdirectory(Tests/ApplicationTests)
    add_subdirectory(Tests/RHITests)
    add_subdirectory(Tests/ShaderCompilerTests)
endif()

if(FLUXION_BUILD_SAMPLES)
    add_subdirectory(Samples/InputDemo)
    add_subdirectory(Samples/VulkanTriangleDemo)
endif()
