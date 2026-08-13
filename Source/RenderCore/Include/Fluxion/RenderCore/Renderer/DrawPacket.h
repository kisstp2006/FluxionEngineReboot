#pragma once

#include <Fluxion/Foundation/Types.h>
#include <Fluxion/RenderCore/Renderer/Material.h>
#include <Fluxion/RenderCore/Renderer/MeshBuffer.h>
#include <Fluxion/RenderCore/Renderer/RenderPipeline.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FluxionDrawPacket
{
    FluxionMeshBufferHandle mesh;
    FluxionMaterialHandle material;
    // Added vs. the original sketch -- Execute needs to know which
    // pipeline to bind per packet, and inferring it from `material` would
    // mean a material can only ever be drawn with one pipeline.
    FluxionRenderPipelineHandle pipeline;
    u32 objectDataOffset;
    u32 layerMask;
} FluxionDrawPacket;

#ifdef __cplusplus
}
#endif
