#pragma once

#include <Fluxion/Foundation/Handle.h>
#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FluxionRenderPipelineCategory
{
    FLUXION_RENDER_PIPELINE_CATEGORY_OPAQUE,
    FLUXION_RENDER_PIPELINE_CATEGORY_TRANSPARENT,
} FluxionRenderPipelineCategory;

FLUXION_DEFINE_HANDLE(FluxionRenderPipelineHandle);

// Does not create a real FluxionRHIPipelineHandle here -- the concrete
// vertex layout isn't known yet. The real pipeline is lazily built and
// cached, keyed by vertex-layout hash, the first time a draw needs it
// for a given FluxionRHIVertexLayout (see RenderPipeline.cpp).
// colorFormat/depthFormat must match the render target this pipeline will
// actually be drawn into (e.g. the swapchain's own format), not an
// assumed default.
FluxionRenderPipelineHandle Fluxion_RenderPipeline_Create(FluxionRHIDeviceHandle device, FluxionShaderProgramHandle program, FluxionRenderPipelineCategory category, FluxionRHIFormat colorFormat, FluxionRHIFormat depthFormat);
void Fluxion_RenderPipeline_Destroy(FluxionRenderPipelineHandle pipeline);

#ifdef __cplusplus
}
#endif
