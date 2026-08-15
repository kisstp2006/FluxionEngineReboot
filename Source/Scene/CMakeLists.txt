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
        # Scene.h names a data component by its FluxionTypeId, so whoever
        # includes it sees the reflection headers too.
        Fluxion::Core
    PRIVATE_DEPENDENCIES
        # The clock and the input state a script reads. Neither appears in
        # a header of this module.
        Fluxion::Application
        # Recognising a component field that points at an asset. Only the
        # field's declared type is needed, and that does not appear in a
        # header of this module either.
        Fluxion::Assets
)
