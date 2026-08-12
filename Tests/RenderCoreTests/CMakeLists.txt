engine_add_test(
    NAME RenderCoreTests
    SOURCES
        main.c
        Test_PassRegistry.c
        Test_GraphCompile.cpp
        Test_JsonLoader.c
        Test_DumpDot.c
    DEPENDENCIES
        Fluxion::RenderCore
        Fluxion::RHI
)

engine_add_header_selfcontained_test(
    MODULE RenderCore
    HEADERS
        Fluxion/RenderCore/RenderGraph/RenderGraphHandles.h
        Fluxion/RenderCore/RenderGraph/RenderGraphResource.h
        Fluxion/RenderCore/RenderGraph/RenderGraphPass.h
        Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h
        Fluxion/RenderCore/RenderGraph/RenderGraph.h
)
