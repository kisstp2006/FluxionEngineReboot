#include "TestFramework.h"

#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/Renderer/RenderTarget.h>
#include <Fluxion/RenderCore/Renderer/RenderView.h>

void Test_RenderView_Run(TestContext* ctx)
{
    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests", false };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &instanceDesc);

    FluxionRHIAdapterHandle adapters[1];
    Fluxion_RHI_EnumerateAdapters(instance, adapters, 1);

    FluxionRHIDeviceDesc deviceDesc = { 0 };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);

    FluxionRHITextureDesc colorTextureDesc;
    colorTextureDesc.width = 64;
    colorTextureDesc.height = 64;
    colorTextureDesc.depth = 1;
    colorTextureDesc.mipLevels = 1;
    colorTextureDesc.arrayLayers = 1;
    colorTextureDesc.sampleCount = 1;
    colorTextureDesc.format = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    colorTextureDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET;
    colorTextureDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    colorTextureDesc.debugName = "Test_RenderView.Color";
    FluxionRHITextureHandle colorTexture = Fluxion_RHI_CreateTexture(device, &colorTextureDesc);

    FluxionRHITextureViewDesc colorViewDesc = { colorTexture, colorTextureDesc.format, 0, 1, 0, 1 };
    FluxionRHITextureViewHandle colorView = Fluxion_RHI_CreateTextureView(device, &colorViewDesc);

    FluxionRenderTargetDesc targetDesc;
    targetDesc.colorViews[0] = colorView;
    targetDesc.colorViewCount = 1;
    targetDesc.depthView.index = FLUXION_HANDLE_INVALID_INDEX;
    targetDesc.depthView.generation = 0;
    FluxionRenderTargetHandle renderTarget = Fluxion_RenderTarget_Create(device, &targetDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(renderTarget));

    FluxionRenderViewDesc viewDesc;
    viewDesc.viewMatrix = Fluxion_Mat4_Identity();
    viewDesc.projectionMatrix = Fluxion_Mat4_Identity();
    viewDesc.viewport.x = 0.0f;
    viewDesc.viewport.y = 0.0f;
    viewDesc.viewport.width = 64.0f;
    viewDesc.viewport.height = 64.0f;
    viewDesc.viewport.minDepth = 0.0f;
    viewDesc.viewport.maxDepth = 1.0f;
    viewDesc.scissor.x = 0;
    viewDesc.scissor.y = 0;
    viewDesc.scissor.width = 64;
    viewDesc.scissor.height = 64;
    viewDesc.renderTarget = renderTarget;
    viewDesc.layerMask = 0xFFFFFFFFu;

    FluxionRenderViewHandle view = Fluxion_RenderView_Create(device, &viewDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(view));

    // Must not assert/crash whether called once or repeatedly.
    Fluxion_RenderView_UpdateFrameConstants(view);
    Fluxion_RenderView_UpdateFrameConstants(view);

    Fluxion_RenderView_Destroy(view);
    Fluxion_RenderTarget_Destroy(renderTarget);
    Fluxion_RHI_DestroyTextureView(colorView);
    Fluxion_RHI_DestroyTexture(colorTexture);

    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}
