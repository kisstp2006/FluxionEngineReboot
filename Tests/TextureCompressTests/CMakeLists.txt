engine_add_test(
    NAME TextureCompressTests
    SOURCES
        main.c
        Test_BlockCompress.c
    DEPENDENCIES
        Fluxion::TextureCompress
)

engine_add_header_selfcontained_test(
    MODULE TextureCompress
    HEADERS
        Fluxion/TextureCompress/BlockCompress.h
)
