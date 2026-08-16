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

# Building for a 64-bit ARM Linux machine from a different one.
#
# Everything here is about telling CMake it is NOT building for the
# machine it is running on. Without CMAKE_SYSTEM_NAME set, CMake treats
# the build as native and will happily try to run the things it compiles
# -- which is how a cross build ends up failing on a test program rather
# than on anything real.
#
# The compilers default to the Debian/Ubuntu cross toolchain names, which
# is what `apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu`
# installs. Pass -DFLUXION_AARCH64_TOOLCHAIN_PREFIX=... to name a
# different one; the prefix is used as-is, so it may be a bare name found
# on PATH or an absolute path.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED FLUXION_AARCH64_TOOLCHAIN_PREFIX)
    set(FLUXION_AARCH64_TOOLCHAIN_PREFIX "aarch64-linux-gnu-")
endif()

set(CMAKE_C_COMPILER   "${FLUXION_AARCH64_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${FLUXION_AARCH64_TOOLCHAIN_PREFIX}g++")

# Where to look for the target's own headers and libraries. The three
# rules below say: find programs on the HOST (the compiler itself, and any
# tool the build runs), but find libraries, headers and packages only
# under the target's root -- otherwise find_package(X11) would happily
# hand back the host's x86 libX11 and the link would fail with an
# architecture mismatch that reads as if the library were missing.
if(NOT DEFINED CMAKE_FIND_ROOT_PATH)
    set(CMAKE_FIND_ROOT_PATH "/usr/${FLUXION_AARCH64_TOOLCHAIN_PREFIX}")
    string(REGEX REPLACE "-$" "" CMAKE_FIND_ROOT_PATH "${CMAKE_FIND_ROOT_PATH}")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Setting CMAKE_SYSTEM_NAME above also sets CMAKE_CROSSCOMPILING, which
# is what keeps the tests from being registered with CTest here (see
# CMake/EngineTest.cmake): they are still built, because compiling them
# checks that the engine's headers and this toolchain agree, but running
# them belongs to whoever has the hardware.
