engine_add_module(
    NAME RenderCore
    TYPE STATIC
    LANGUAGE C CXX
    PUBLIC_DEPENDENCIES
        Fluxion::RHI
        Fluxion::Foundation
        # MeshAsset.h names an asset type by its id, so whoever includes
        # it sees the asset headers too.
        Fluxion::Assets
    PRIVATE_DEPENDENCIES
        pdjson
        Fluxion::ShaderCompiler
        # For the job system, so a shader reload's compiling can happen off
        # the frame. Private: nothing in this module's headers mentions a
        # job, and a caller does not have to know one was used.
        Fluxion::Core
)
