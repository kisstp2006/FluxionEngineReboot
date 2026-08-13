engine_add_module(
    NAME Scene
    TYPE STATIC
    LANGUAGE C CXX
    NO_EXCEPTIONS
    NO_RTTI
    PUBLIC_DEPENDENCIES
        Fluxion::Foundation
        Fluxion::Script
        # EngineScript.hpp names a renderer, a mesh, a material and a
        # pipeline in its own signatures, so whoever includes it sees
        # those headers too.
        Fluxion::RenderCore
    PRIVATE_DEPENDENCIES
        Fluxion::Core
        # The clock and the input state a script reads. Neither appears in
        # a header of this module.
        Fluxion::Application
)
