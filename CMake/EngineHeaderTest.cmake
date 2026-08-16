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

include_guard(GLOBAL)

# engine_add_header_selfcontained_test(
#     MODULE <name>              # existing module target (Fluxion::<name> alias must exist)
#     HEADERS <include-path>...  # e.g. Fluxion/Core/Startup/Subsystem.hpp
# )
#
# For every header, generates a standalone translation unit whose only
# content is `#include <that-header>` -- proving the header compiles with no
# reliance on include order or on some other header having pulled in its
# dependencies first. A `.h` header is compiled as C (a generated `.c` TU);
# a `.hpp` header is compiled as C++ (a generated `.cpp` TU) -- this
# codebase's own convention is `.h` = C-compatible, `.hpp` = C++-only, so a
# `.h` header that only ever got compiled as C++ here would never actually
# prove it also compiles as plain C. All generated TUs, plus one shared
# main(), link into a single `<Module>HeaderSelfContainedTests` executable
# registered with CTest.
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

        if(ENGINE_HEADER_TEST_HEADER MATCHES "\\.hpp$")
            set(ENGINE_HEADER_TEST_EXT "cpp")
        else()
            set(ENGINE_HEADER_TEST_EXT "c")
        endif()

        set(ENGINE_HEADER_TEST_TU "${ENGINE_HEADER_TEST_GENERATED_DIR}/${ENGINE_HEADER_TEST_ID}.${ENGINE_HEADER_TEST_EXT}")
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
