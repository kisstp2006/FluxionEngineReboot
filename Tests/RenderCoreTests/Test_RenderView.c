// The contents of this file are subject to the Common Public Attribution
// License Version 1.0 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// https://opensource.org/license/cpal-1-0. The License is based on the
// Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
// to cover use of software over a computer network and provide for limited
// attribution for the Original Developer. In addition, Exhibit A has been
// modified to be consistent with Exhibit B.
//
// Software distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
// for the specific language governing rights and limitations under the
// License.
//
// The Original Code is Fluxion Engine.
//
// The Original Developer is not the Initial Developer and is __________. If
// left blank, the Original Developer is the Initial Developer.
//
// The Initial Developer of the Original Code is Kiss Tibor Péter. All
// portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
// All Rights Reserved.
//
// Contributor ______________________.
//
// Alternatively, the contents of this file may be used under the terms of
// the Fluxion Engine Commercial License Agreement Version 1.0, separately
// obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
// License"), in which case the provisions of the Commercial License are
// applicable instead of those above.
//
// If you wish to allow use of your version of this file only under the terms
// of the Commercial License and not to allow others to use your version of
// this file under the CPAL, indicate your decision by deleting the
// provisions above and replace them with the notice and other provisions
// required by the Commercial License. If you do not delete the provisions
// above, a recipient may use your version of this file under either the CPAL
// or the Commercial License.
//
// SPDX-License-Identifier: CPAL-1.0

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

    FluxionRHITextureViewDesc colorViewDesc = { colorTexture, colorTextureDesc.format, 0, 1, 0, 1, FLUXION_RHI_TEXTURE_DIMENSION_2D };
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
