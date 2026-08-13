engine_add_module(
    NAME Scene
    TYPE STATIC
    LANGUAGE C CXX
    NO_EXCEPTIONS
    NO_RTTI
    PUBLIC_DEPENDENCIES
        Fluxion::Foundation
        Fluxion::Script
    PRIVATE_DEPENDENCIES
        Fluxion::Core
)
