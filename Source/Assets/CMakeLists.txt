engine_add_module(
    NAME Assets
    TYPE STATIC
    LANGUAGE C CXX
    NO_EXCEPTIONS
    NO_RTTI
    PUBLIC_DEPENDENCIES
        Fluxion::Foundation
        # AssetRef.h names a FluxionTypeId, AssetService.h embeds a
        # FluxionServiceHeader and the loader runs on the job system, so
        # whoever includes those headers sees Core's too.
        Fluxion::Core
    PRIVATE_DEPENDENCIES
        # Real files on a real disk, behind the directory source. Nothing
        # in a header of this module names it -- that is the point of the
        # source interface.
        Fluxion::Platform
)
