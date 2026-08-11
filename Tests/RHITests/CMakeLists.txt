engine_add_test(
    NAME RHITests
    SOURCES
        main.c
        Test_Handles.c
        Test_Capabilities.c
        Test_NullBackend.c
        Test_NativeHandle.c
    DEPENDENCIES
        Fluxion::RHI
)

engine_add_header_selfcontained_test(
    MODULE RHI
    HEADERS
        Fluxion/RHI/Handles.h
        Fluxion/RHI/Capabilities.h
        Fluxion/RHI/Adapter.h
        Fluxion/RHI/Format.h
        Fluxion/RHI/ResourceState.h
        Fluxion/RHI/ResourceDesc.h
        Fluxion/RHI/NativeHandle.h
        Fluxion/RHI/RHI.h
)
