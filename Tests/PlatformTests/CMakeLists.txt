add_subdirectory(TestPlugin)

engine_add_test(
    NAME PlatformTests
    SOURCES
        main.c
        Test_Memory.c
        Test_Time.c
        Test_CPU.c
        Test_Debug.c
        Test_Environment.c
        Test_Process.c
        Test_File.c
        Test_DynamicLibrary.c
        Test_Thread.c
        Test_Synchronization.c
        Test_Semaphore.c
    DEPENDENCIES
        Fluxion::Platform
        Fluxion::Foundation
)

add_dependencies(PlatformTests PlatformTestPlugin)
target_compile_definitions(PlatformTests PRIVATE
    FLUXION_TEST_PLUGIN_PATH="$<TARGET_FILE:PlatformTestPlugin>"
)

# Gives Test_Environment.c a variable with a known, fixed value to check —
# unlike PATH, which varies wildly in length across machines.
set_tests_properties(PlatformTests PROPERTIES
    ENVIRONMENT "FLUXION_TEST_ENV_VAR=FluxionValue123"
)

engine_add_header_selfcontained_test(
    MODULE Platform
    HEADERS
        Fluxion/Platform/CPU.h
        Fluxion/Platform/Debug.h
        Fluxion/Platform/DynamicLibrary.h
        Fluxion/Platform/Environment.h
        Fluxion/Platform/File.h
        Fluxion/Platform/Memory.h
        Fluxion/Platform/Process.h
        Fluxion/Platform/Semaphore.h
        Fluxion/Platform/Synchronization.h
        Fluxion/Platform/Thread.h
        Fluxion/Platform/Time.h
)
