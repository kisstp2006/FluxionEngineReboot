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

engine_add_test(
    NAME CoreTests
    SOURCES
        main.c
        Test_Reflection.c
        Test_Plugin.c
        Test_Startup.c
        Test_StartupCpp.cpp
        Test_PluginSubsystem.c
    DEPENDENCIES
        Fluxion::Core
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
)
