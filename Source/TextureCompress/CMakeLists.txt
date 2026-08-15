engine_add_module(
    NAME TextureCompress
    TYPE STATIC
    LANGUAGE C
    PUBLIC_DEPENDENCIES
        Fluxion::Foundation
        # The public header names a FluxionRHIFormat and asks the format
        # table how large a level is -- a block format is a format before
        # it is anything else, and there is one place that knows what each
        # one costs.
        Fluxion::RHI
)
