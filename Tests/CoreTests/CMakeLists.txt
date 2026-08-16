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

# Builds a tiny SHARED plugin fixture. Called from inside the fixture's
# own <NAME>/CMakeLists.txt (via add_subdirectory below), so the source
# file is just "<NAME>.c" relative to that directory — not "<NAME>/<NAME>.c".
# Grants just enough include access to see Fluxion/Core/Plugin/ABI.h (and
# the Foundation headers it pulls in) without statically linking the whole
# Core module into the plugin.
function(add_test_plugin NAME)
    add_library(${NAME} SHARED "${NAME}.c")
    target_include_directories(${NAME} PRIVATE
        $<TARGET_PROPERTY:Core,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:Foundation,INTERFACE_INCLUDE_DIRECTORIES>
    )
endfunction()

add_subdirectory(HelloPlugin)
add_subdirectory(HelloPluginDependent)
add_subdirectory(CyclicPluginA)
add_subdirectory(CyclicPluginB)
add_subdirectory(PluginSubsystemPlugin)

set(CORE_TESTS_SOURCES
    main.c
    Test_Reflection.c
    Test_Plugin.c
    Test_Startup.c
    Test_StartupCpp.cpp
    Test_PluginSubsystem.c
    Test_Service.c
    Test_ServiceCpp.cpp
    Test_ReflectionCpp.cpp
    Test_Jobs.c
    Test_JobsCpp.cpp
    Test_BinarySerializer.c
)

# Excluded (rather than compiled-but-failing) when FLUXION_PROFILING is
# OFF: the Profiler API itself compiles down to no-ops in that
# configuration (Profiler.h), so these tests' own assertions about
# backend calls actually being made would not hold.
if(FLUXION_PROFILING)
    list(APPEND CORE_TESTS_SOURCES Test_Profile.c Test_ProfileCpp.cpp)
endif()

# PCH lives here rather than on the Core module itself -- Core's own
# Private/ sources are entirely .c (its C++ facades like Service.hpp/
# Jobs.hpp are header-only, consumed by whoever includes them), so
# CoreTests is the target that actually compiles a meaningful number of
# C++ TUs against these headers.
engine_add_test(
    NAME CoreTests
    SOURCES ${CORE_TESTS_SOURCES}
    DEPENDENCIES
        Fluxion::Core
    NO_EXCEPTIONS
    NO_RTTI
    PCH
        <type_traits>
        <span>
        <cstring>
)

add_dependencies(CoreTests HelloPlugin HelloPluginDependent CyclicPluginA CyclicPluginB PluginSubsystemPlugin)

target_compile_definitions(CoreTests PRIVATE
    FLUXION_TEST_HELLO_PLUGIN_PATH="$<TARGET_FILE_DIR:HelloPlugin>/HelloPlugin.plugin"
    FLUXION_TEST_HELLO_PLUGIN_DEPENDENT_PATH="$<TARGET_FILE_DIR:HelloPluginDependent>/HelloPluginDependent.plugin"
    FLUXION_TEST_CYCLIC_PLUGIN_A_PATH="$<TARGET_FILE_DIR:CyclicPluginA>/CyclicPluginA.plugin"
    FLUXION_TEST_CYCLIC_PLUGIN_B_PATH="$<TARGET_FILE_DIR:CyclicPluginB>/CyclicPluginB.plugin"
    FLUXION_TEST_BAD_PLUGIN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/BadPlugin.plugin"
    FLUXION_TEST_PLUGIN_SUBSYSTEM_PLUGIN_PATH="$<TARGET_FILE_DIR:PluginSubsystemPlugin>/PluginSubsystemPlugin.plugin"
)

engine_add_header_selfcontained_test(
    MODULE Core
    HEADERS
        Fluxion/Core/Startup/StartupPhase.h
        Fluxion/Core/Startup/SubsystemId.h
        Fluxion/Core/Startup/SubsystemDesc.h
        Fluxion/Core/Startup/SubsystemRegistry.h
        Fluxion/Core/Startup/Subsystem.hpp
        Fluxion/Core/Service/ServiceId.h
        Fluxion/Core/Service/ServiceHeader.h
        Fluxion/Core/Service/ServiceRegistry.h
        Fluxion/Core/Service/Service.hpp
        Fluxion/Core/Reflection/MethodInfo.h
        Fluxion/Core/Reflection/Reflection.hpp
        Fluxion/Core/Jobs/JobHandle.h
        Fluxion/Core/Jobs/JobDesc.h
        Fluxion/Core/Jobs/JobSystem.h
        Fluxion/Core/Jobs/Jobs.hpp
        Fluxion/Core/Serialization/BinarySerializer.h
        Fluxion/Core/Diagnostics/ProfileBackend.h
        Fluxion/Core/Diagnostics/Profiler.h
        Fluxion/Core/Diagnostics/ProfileScope.hpp
        Fluxion/Core/Plugin/ABI.hpp
)
