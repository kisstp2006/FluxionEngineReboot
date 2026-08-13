engine_add_module(
    NAME Script
    TYPE STATIC
    LANGUAGE CXX
    PUBLIC_DEPENDENCIES
        Fluxion::Foundation
        Fluxion::Core
    NO_EXCEPTIONS
    NO_RTTI
)
