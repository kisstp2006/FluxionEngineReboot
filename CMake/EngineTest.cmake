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

# engine_add_test(
#     NAME <name>
#     SOURCES <file>...
#     [DEPENDENCIES <target>...]
#     [NO_EXCEPTIONS]                    # disable C++ exceptions on this target's CXX sources
#     [NO_RTTI]                          # disable C++ RTTI on this target's CXX sources
#     [PCH <header>...]                  # optional: precompiled headers, private to this executable's own TUs
# )
#
# Builds a plain executable and registers it with CTest. No external test
# framework — the executable itself decides pass/fail via its exit code.
function(engine_add_test)
    set(options NO_EXCEPTIONS NO_RTTI NO_TEST_REGISTRATION)
    set(oneValueArgs NAME)
    set(multiValueArgs SOURCES DEPENDENCIES PCH)
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

    if(ENGINE_TEST_NO_EXCEPTIONS OR ENGINE_TEST_NO_RTTI)
        set(ENGINE_TEST_CPP_POLICY_ARGS)
        if(ENGINE_TEST_NO_EXCEPTIONS)
            list(APPEND ENGINE_TEST_CPP_POLICY_ARGS NO_EXCEPTIONS)
        endif()
        if(ENGINE_TEST_NO_RTTI)
            list(APPEND ENGINE_TEST_CPP_POLICY_ARGS NO_RTTI)
        endif()
        engine_set_cpp_policy(${ENGINE_TEST_NAME} ${ENGINE_TEST_CPP_POLICY_ARGS})
    endif()

    if(ENGINE_TEST_PCH)
        # Scoped to CXX TUs only (same reasoning as engine_add_module's
        # PCH handling) -- most test executables mix .c and .cpp sources.
        set(ENGINE_TEST_PCH_SCOPED)
        foreach(ENGINE_TEST_PCH_HEADER ${ENGINE_TEST_PCH})
            if(ENGINE_TEST_PCH_HEADER MATCHES "^<(.*)>$")
                # See EngineModule.cmake's PCH handling for why $<ANGLE-R>
                # is needed here instead of a literal closing bracket.
                list(APPEND ENGINE_TEST_PCH_SCOPED "$<$<COMPILE_LANGUAGE:CXX>:<${CMAKE_MATCH_1}$<ANGLE-R>>")
            else()
                list(APPEND ENGINE_TEST_PCH_SCOPED "$<$<COMPILE_LANGUAGE:CXX>:${ENGINE_TEST_PCH_HEADER}>")
            endif()
        endforeach()
        target_precompile_headers(${ENGINE_TEST_NAME} PRIVATE ${ENGINE_TEST_PCH_SCOPED})
    endif()

    # Built either way -- compiling the tests is itself a check that the
    # engine's headers and this target's toolchain agree -- but only
    # registered with CTest when the result can actually be started here.
    # A cross build produces binaries for another machine, and running
    # them would fail for a reason that has nothing to do with the code.
    #
    # NO_TEST_REGISTRATION builds the binary without the CTest entry, for
    # a suite the caller registers itself in slices -- see ScriptTests,
    # whose one entry dominated the whole suite's wall clock.
    if(NOT CMAKE_CROSSCOMPILING AND NOT ENGINE_TEST_NO_TEST_REGISTRATION)
        add_test(NAME ${ENGINE_TEST_NAME} COMMAND ${ENGINE_TEST_NAME})
    endif()
endfunction()
