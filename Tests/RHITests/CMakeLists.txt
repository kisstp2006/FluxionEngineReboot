set(RHITESTS_SOURCES
    main.c
    Test_BackendAvailability.c
    Test_Handles.c
    Test_Capabilities.c
    Test_NullBackend.c
    Test_NativeHandle.c
    Test_VulkanBackend.c
    Test_OpenGLBackend.c
    Test_BindGroup.c
    Test_PipelineCacheFile.cpp
    Test_Timestamps.c
    Test_Validation.c
)
if(WIN32)
    # D3D12 doesn't exist outside Windows -- same reasoning as
    # Source/RHI/CMakeLists.txt excluding Private/D3D12 elsewhere.
    list(APPEND RHITESTS_SOURCES Test_D3D12Backend.c)
endif()

engine_add_test(
    NAME RHITests
    SOURCES ${RHITESTS_SOURCES}
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
