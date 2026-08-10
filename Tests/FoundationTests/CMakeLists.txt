engine_add_test(
    NAME FoundationTests
    SOURCES
        main.c
        Test_Types.c
        Test_Assert.c
        Test_Memory.c
        Test_Containers.c
        Test_Hashing.c
        Test_Handle.c
        Test_UUID.c
        Test_Bit.c
        Test_Atomic.c
        Test_Math.c
    DEPENDENCIES
        Fluxion::Foundation
)
