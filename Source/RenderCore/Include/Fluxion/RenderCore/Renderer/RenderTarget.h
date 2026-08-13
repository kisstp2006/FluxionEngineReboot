#pragma once

#include <Fluxion/Foundation/Handle.h>
#include <Fluxion/RHI/RHI.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLUXION_RENDER_TARGET_MAX_COLOR_ATTACHMENTS 4

FLUXION_DEFINE_HANDLE(FluxionRenderTargetHandle);

typedef struct FluxionRenderTargetDesc
{
    FluxionRHITextureViewHandle colorViews[FLUXION_RENDER_TARGET_MAX_COLOR_ATTACHMENTS];
    u32 colorViewCount;
    FluxionRHITextureViewHandle depthView; // optional -- default-initialized (invalid) if none
} FluxionRenderTargetDesc;

FluxionRenderTargetHandle Fluxion_RenderTarget_Create(FluxionRHIDeviceHandle device, const FluxionRenderTargetDesc* desc);
void Fluxion_RenderTarget_Destroy(FluxionRenderTargetHandle target);

#ifdef __cplusplus
}
#endif
