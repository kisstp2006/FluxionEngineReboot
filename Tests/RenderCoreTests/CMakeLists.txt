engine_add_test(
    NAME RenderCoreTests
    SOURCES
        main.c
        Test_PassRegistry.c
        Test_GraphCompile.cpp
        Test_JsonLoader.c
        Test_DumpDot.c
        Test_ShaderProgram.cpp
        Test_ShaderReload.cpp
        Test_Material.cpp
        Test_MeshBuffer.c
        Test_MemoryDomains.c
        Test_RenderView.c
        Test_RendererFrame.cpp
        Test_MeshAsset.cpp
    DEPENDENCIES
        Fluxion::RenderCore
        Fluxion::RHI
        Fluxion::ShaderCompiler
        # The shader reload checks start and stop a job system, to cover
        # both the arrangement a real host has and the one where nobody
        # started any workers at all.
        Fluxion::Core
)

engine_add_header_selfcontained_test(
    MODULE RenderCore
    HEADERS
        Fluxion/RenderCore/RenderGraph/RenderGraphHandles.h
        Fluxion/RenderCore/RenderGraph/RenderGraphResource.h
        Fluxion/RenderCore/RenderGraph/RenderGraphPass.h
        Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h
        Fluxion/RenderCore/RenderGraph/RenderGraph.h
        Fluxion/RenderCore/Renderer/MeshBuffer.h
        Fluxion/RenderCore/Renderer/MeshAsset.h
)
