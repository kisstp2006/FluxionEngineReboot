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
        Test_ResultCpp.cpp
        Test_HandleCpp.cpp
        Test_SpanStringViewCpp.cpp
        Test_MathCpp.cpp
        Test_FoundationUtilCpp.cpp
    DEPENDENCIES
        Fluxion::Foundation
    NO_EXCEPTIONS
    NO_RTTI
)

engine_add_header_selfcontained_test(
    MODULE Foundation
    HEADERS
        Fluxion/Foundation/Result.hpp
        Fluxion/Foundation/Handle.hpp
        Fluxion/Foundation/Containers/Span.hpp
        Fluxion/Foundation/Containers/StringView.hpp
        Fluxion/Foundation/Math.hpp
        Fluxion/Foundation/Memory/AllocatorRef.hpp
        Fluxion/Foundation/ScopeExit.hpp
        Fluxion/Foundation/FunctionRef.hpp
        Fluxion/Foundation/BitFlags.hpp
)
