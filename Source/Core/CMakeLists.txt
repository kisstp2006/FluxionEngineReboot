engine_add_module(
    NAME Core
    TYPE STATIC
    LANGUAGE C CXX
    NO_EXCEPTIONS
    NO_RTTI
    PUBLIC_DEPENDENCIES
        Fluxion::Foundation
    PRIVATE_DEPENDENCIES
        Fluxion::Platform
        pdjson
)
