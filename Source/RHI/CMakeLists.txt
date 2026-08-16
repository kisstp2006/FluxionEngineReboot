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

# Which backends this build contains.
#
# Vulkan is the one that runs everywhere the engine targets, so it is the
# baseline. The other two are tied to something the target either has or
# does not: D3D12 is a Windows API, and Private/OpenGL is DESKTOP OpenGL
# reached through WGL or GLX -- not GL ES through EGL, which is a
# different set of entry points and a different surface, and would be its
# own backend rather than a flag on this one.
set(FLUXION_RHI_BACKEND_VULKAN ON)

if(WIN32)
    set(FLUXION_RHI_BACKEND_D3D12 ON)
else()
    set(FLUXION_RHI_BACKEND_D3D12 OFF)
endif()

if(FLUXION_HAS_DESKTOP_OPENGL)
    set(FLUXION_RHI_BACKEND_OPENGL ON)
else()
    set(FLUXION_RHI_BACKEND_OPENGL OFF)
endif()

find_package(Vulkan REQUIRED)
if(FLUXION_RHI_BACKEND_OPENGL)
    find_package(OpenGL REQUIRED)
endif()

# An explicit source list rather than the module's own auto-glob, per
# engine_add_module's documented escape hatch for "sources differ per OS":
# a backend that is off must not be compiled at all, because each one
# links against libraries the target may not have.
file(GLOB_RECURSE RHI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
)
if(NOT FLUXION_RHI_BACKEND_D3D12)
    list(FILTER RHI_SOURCES EXCLUDE REGEX "/Private/D3D12/")
endif()
if(NOT FLUXION_RHI_BACKEND_OPENGL)
    list(FILTER RHI_SOURCES EXCLUDE REGEX "/Private/OpenGL/")
endif()

engine_add_module(
    NAME RHI
    TYPE STATIC
    LANGUAGE C CXX
    SOURCES ${RHI_SOURCES}
    PUBLIC_DEPENDENCIES
        Fluxion::Foundation
        Fluxion::Application
    PRIVATE_DEPENDENCIES
        Fluxion::Platform
)

# What Private/RHI.c compiles its backend table from. Kept PRIVATE: a
# caller asks Fluxion_RHI_IsBackendAvailable at runtime rather than
# reading the build's own configuration out of a macro, so that a program
# built once against this library cannot disagree with it.
foreach(backend VULKAN OPENGL D3D12)
    if(FLUXION_RHI_BACKEND_${backend})
        target_compile_definitions(RHI PRIVATE FLUXION_RHI_HAS_${backend}=1)
    else()
        target_compile_definitions(RHI PRIVATE FLUXION_RHI_HAS_${backend}=0)
    endif()
endforeach()

# Vulkan::Vulkan/vma/OpenGL::GL/glheaders are all PRIVATE: backend-native
# graphics types never leak into the portable API, so the public RHI
# headers never mention a Vulkan, VMA, or OpenGL type, and nothing outside
# Private/Vulkan or Private/OpenGL needs these include paths or link
# inputs.
target_link_libraries(RHI PRIVATE Vulkan::Vulkan vma)

if(FLUXION_RHI_BACKEND_OPENGL)
    target_link_libraries(RHI PRIVATE OpenGL::GL glheaders)
endif()

if(FLUXION_RHI_BACKEND_OPENGL AND FLUXION_PLATFORM_NAME STREQUAL "Linux")
    # The GLX path in Private/OpenGL needs X11/Xlib.h + libX11 -- already a
    # transitive dependency of Application's own Xlib backend, linked here
    # explicitly too so RHI doesn't rely on that being incidentally true.
    find_package(X11 REQUIRED)
    target_include_directories(RHI PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(RHI PRIVATE ${X11_LIBRARIES})
endif()

if(FLUXION_RHI_BACKEND_D3D12)
    # d3d12/dxgi are the OS-supplied import libs (declarations come from
    # the Windows SDK, already on MSVC's default include path -- no
    # find_package/vendored-header step needed, unlike Vulkan's SDK or
    # OpenGL's missing GL1.2+/WGL-extension headers).
    target_link_libraries(RHI PRIVATE d3d12 dxgi d3d12ma)
endif()
