engine_add_module(
    NAME RenderCore
    TYPE STATIC
    LANGUAGE C CXX
    PUBLIC_DEPENDENCIES
        Fluxion::RHI
        Fluxion::Foundation
    PRIVATE_DEPENDENCIES
        pdjson
        Fluxion::ShaderCompiler
)
