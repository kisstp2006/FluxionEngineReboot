# The contents of this file are subject to the Common Public Attribution
# License Version 1.0 (the "License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# https://opensource.org/license/cpal-1-0. The License is based on the
# Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
# to cover use of software over a computer network and provide for limited
# attribution for the Original Developer. In addition, Exhibit A has been
# modified to be consistent with Exhibit B.
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is Fluxion Engine.
#
# The Original Developer is not the Initial Developer and is __________. If
# left blank, the Original Developer is the Initial Developer.
#
# The Initial Developer of the Original Code is Kiss Tibor Péter. All
# portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
# All Rights Reserved.
#
# Contributor ______________________.
#
# Alternatively, the contents of this file may be used under the terms of
# the Fluxion Engine Commercial License Agreement Version 1.0, separately
# obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
# License"), in which case the provisions of the Commercial License are
# applicable instead of those above.
#
# If you wish to allow use of your version of this file only under the terms
# of the Commercial License and not to allow others to use your version of
# this file under the CPAL, indicate your decision by deleting the
# provisions above and replace them with the notice and other provisions
# required by the Commercial License. If you do not delete the provisions
# above, a recipient may use your version of this file under either the CPAL
# or the Commercial License.
#
# SPDX-License-Identifier: CPAL-1.0

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
add_subdirectory(ThirdParty/nuklear)
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
add_subdirectory(Source/Assets)
add_subdirectory(Source/Application)
add_subdirectory(Source/RHI)
add_subdirectory(Source/ShaderCompiler)
add_subdirectory(Source/TextureCompress)
add_subdirectory(Source/Script)
add_subdirectory(Source/RenderCore)
add_subdirectory(Source/DebugUI)
add_subdirectory(Source/Scene)

if(FLUXION_BUILD_TESTS)
    add_subdirectory(Tests/FoundationTests)
    add_subdirectory(Tests/PlatformTests)
    add_subdirectory(Tests/CoreTests)
    add_subdirectory(Tests/AssetsTests)
    add_subdirectory(Tests/ApplicationTests)
    add_subdirectory(Tests/RHITests)
    add_subdirectory(Tests/ShaderCompilerTests)
    add_subdirectory(Tests/TextureCompressTests)
    add_subdirectory(Tests/ScriptTests)
    add_subdirectory(Tests/RenderCoreTests)
    add_subdirectory(Tests/DebugUITests)
    add_subdirectory(Tests/SceneTests)

    # Not a compiled test: it reads the top of every source file the engine
    # owns and fails if one goes out without the license notice. Cheap, and
    # it turns "remember to add the header" from a habit into a rule.
    add_test(NAME LicenseHeaders
             COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/CMake/CheckLicenseHeaders.cmake")
endif()

if(FLUXION_BUILD_SAMPLES)
    add_subdirectory(Samples/InputDemo)
    add_subdirectory(Samples/ForwardRendererDemo)
endif()
