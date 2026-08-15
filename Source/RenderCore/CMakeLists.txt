include(EngineShaderLibrary)

# The engine's own shader library, carried in the binary rather than
# looked for at runtime. Order matters here only in that the generated
# source has to exist before the module claims it.
engine_embed_shader_library(
    OUTPUT FLUXION_SHADER_LIBRARY_GENERATED
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
    FILES
        Fluxion/Math.jsl
        Fluxion/Surface.jsl
        Fluxion/Frame.jsl
        Fluxion/Object.jsl
        Fluxion/Varyings.jsl
        Fluxion/BRDF.jsl
        Fluxion/Lighting.jsl
        Fluxion/Material.jsl
        Fluxion/Pass/Vertex.jsl
        Fluxion/Pass/Forward.jsl
        Fluxion/Pass/DepthOnly.jsl
)

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

# Added after the module rather than through its SOURCES, so the auto-glob
# of Private/ keeps working for everything else.
target_sources(RenderCore PRIVATE "${FLUXION_SHADER_LIBRARY_GENERATED}")
