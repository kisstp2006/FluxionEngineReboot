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

#include <string.h>

// Engine-internal, and named here rather than reached through a header
// the module keeps private. What it reports is not a detail: it decides
// whether the environment passes run this frame at all.
bool FluxionRendererInternal_RenderView_TakeEnvironmentDirty(FluxionRenderViewHandle view);

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

    // Zeroed first, not filled field by field: the description has
    // fields whose zero means "the engine's own default" -- the shadow
    // atlas sizes among them -- and a stack description that skipped one
    // would be asking for whatever was on the stack.
    FluxionRenderViewDesc viewDesc;
    memset(&viewDesc, 0, sizeof(viewDesc));
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

    // --- Setting the same sky again asks for no work -------------------
    //
    // What answers this flag is a compute pass that overwrites textures
    // the frames still in flight are reading, so a caller handing over
    // the same environment every frame -- which is the ordinary way to
    // write a frame loop -- must not keep it raised. It once did, and
    // the picture went black on one backend a few frames in.
    {
        FluxionRHITextureDesc skyDesc = colorTextureDesc;
        skyDesc.arrayLayers = FLUXION_RHI_CUBE_FACE_COUNT;
        skyDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_SAMPLED;
        skyDesc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_CUBE;
        skyDesc.debugName = "Test_RenderView.Sky";
        FluxionRHITextureHandle skyTexture = Fluxion_RHI_CreateTexture(device, &skyDesc);

        FluxionRHITextureViewDesc skyViewDesc = { skyTexture, skyDesc.format, 0, 1, 0,
                                                  FLUXION_RHI_CUBE_FACE_COUNT, FLUXION_RHI_TEXTURE_DIMENSION_CUBE };
        FluxionRHITextureViewHandle skyView = Fluxion_RHI_CreateTextureView(device, &skyViewDesc);

        FluxionRHISamplerDesc skySamplerDesc = { 0 };
        skySamplerDesc.maxAnisotropy = 1.0f;
        skySamplerDesc.debugName = "Test_RenderView.SkySampler";
        FluxionRHISamplerHandle skySampler = Fluxion_RHI_CreateSampler(device, &skySamplerDesc);

        // A fresh view owes one projection: its buffers hold whatever the
        // allocator handed over until something writes them.
        TEST_CHECK(ctx, FluxionRendererInternal_RenderView_TakeEnvironmentDirty(view));
        TEST_CHECK(ctx, !FluxionRendererInternal_RenderView_TakeEnvironmentDirty(view));

        Fluxion_RenderView_SetEnvironment(view, skyView, skySampler, 1.0f);
        TEST_CHECK(ctx, FluxionRendererInternal_RenderView_TakeEnvironmentDirty(view));

        Fluxion_RenderView_SetEnvironment(view, skyView, skySampler, 1.0f);
        TEST_CHECK(ctx, !FluxionRendererInternal_RenderView_TakeEnvironmentDirty(view));

        // The intensity is not part of what the passes produce -- it
        // travels in the frame constants -- so changing it asks for none
        // of that work either.
        Fluxion_RenderView_SetEnvironment(view, skyView, skySampler, 4.0f);
        TEST_CHECK(ctx, !FluxionRendererInternal_RenderView_TakeEnvironmentDirty(view));

        Fluxion_RHI_DestroySampler(skySampler);
        Fluxion_RHI_DestroyTextureView(skyView);
        Fluxion_RHI_DestroyTexture(skyTexture);
    }

    // --- What fits in the atlas, and what is said about what does not ---
    //
    // The count that comes back is the whole report. A caller that asked
    // for more shadows than there is room for gets the ones that fitted
    // and a number smaller than it asked for -- never silence, and never
    // a shadow that stops appearing without anything saying so.
    {
        u32 atlasSize = 0;
        u32 tileSize = 0;
        Fluxion_RenderView_GetShadowAtlasSize(view, &atlasSize, &tileSize);
        TEST_CHECK(ctx, tileSize > 0 && atlasSize % tileSize == 0);

        const u32 tilesAcross = atlasSize / tileSize;
        const u32 capacity = tilesAcross * tilesAcross;

        // A scene's worth of shapes at once: a sun's four cascades, a
        // spot's single map, and two point lights' cubes. Each light's
        // shadows next to each other, which is the contract a surface
        // relies on to find them.
        FluxionRenderViewShadow shadows[FLUXION_RENDER_VIEW_MAX_SHADOWS] = { 0 };
        const u32 perLight[3] = { 4, 6, 6 };

        u32 offered = 0;
        for (u32 light = 0; light < 3; ++light)
        {
            for (u32 i = 0; i < perLight[light] && offered < FLUXION_RENDER_VIEW_MAX_SHADOWS; ++i)
            {
                shadows[offered].lightViewProjection = Fluxion_Mat4_Identity();
                shadows[offered].lightIndex = light;

                // Near to far, which is the order one light's cascades
                // must be in for a surface to find the sharpest one
                // covering it.
                shadows[offered].coverTo = 10.0f * (f32)(i + 1);
                shadows[offered].cubeFaces = perLight[light] == 6;
                ++offered;
            }
        }

        // The budget and the atlas are the same size on purpose, so a
        // caller that stays inside the first is never refused by the
        // second -- which is what makes the count below a check on the
        // grouping rather than on the packing. What happens when a light
        // does NOT fit is the allocator's answer, and Test_ShadowAtlas
        // is where that is asked.
        TEST_CHECK(ctx, offered == capacity);
        TEST_CHECK(ctx, Fluxion_RenderView_SetShadows(view, shadows, offered) == offered);

        // None is a picture, not a fault.
        TEST_CHECK(ctx, Fluxion_RenderView_SetShadows(view, NULL, 0) == 0);
    }

    // --- The next frame's camera, without building the view again -----
    //
    // A view owns a shadow atlas, a prefiltered environment chain and a
    // lookup table -- and a frame loop that made one per frame spent more
    // time allocating and freeing them than drawing. What this checks is
    // the part that makes keeping a view possible: that being handed a
    // new description moves the camera and leaves the memory alone.
    {
        const FluxionRHITextureHandle atlasBefore = Fluxion_RenderView_GetShadowAtlasTexture(view);
        const FluxionMat4 beforeMatrix = Fluxion_RenderView_GetViewProjection(view);

        FluxionVec3 sideways = { 3.0f, 0.0f, 0.0f };
        FluxionRenderViewDesc moved = viewDesc;
        moved.viewMatrix = Fluxion_Mat4_Translation(sideways);

        TEST_CHECK(ctx, Fluxion_RenderView_UpdateDescription(view, &moved));

        const FluxionMat4 afterMatrix = Fluxion_RenderView_GetViewProjection(view);
        TEST_CHECK(ctx, memcmp(&afterMatrix, &beforeMatrix, sizeof(FluxionMat4)) != 0);

        // AND THE SAME TEXTURE IS STILL THERE. This is the whole claim:
        // the same atlas, not an equal one -- a rebuilt view would hand
        // back a different handle, and would have paid for it.
        const FluxionRHITextureHandle atlasAfter = Fluxion_RenderView_GetShadowAtlasTexture(view);
        TEST_CHECK(ctx, atlasAfter.index == atlasBefore.index && atlasAfter.generation == atlasBefore.generation);

        // A DIFFERENT ATLAS IS REFUSED, AND CHANGES NOTHING. That one is
        // a texture rather than a number, and the frame bind group names
        // it -- a caller asking for another size is asking for another
        // view, and has to be told so rather than quietly given the old
        // size back.
        u32 atlasSize = 0;
        u32 tileSize = 0;
        Fluxion_RenderView_GetShadowAtlasSize(view, &atlasSize, &tileSize);
        TEST_CHECK(ctx, atlasSize > tileSize && tileSize != 0);

        FluxionVec3 further = { 9.0f, 0.0f, 0.0f };
        FluxionRenderViewDesc resized = moved;
        resized.shadowAtlasSize = atlasSize / 2u;
        resized.shadowTileSize = tileSize;
        resized.viewMatrix = Fluxion_Mat4_Translation(further);

        TEST_CHECK(ctx, !Fluxion_RenderView_UpdateDescription(view, &resized));

        // Refused means refused entirely: the camera it also carried did
        // not arrive either.
        const FluxionMat4 afterRefusal = Fluxion_RenderView_GetViewProjection(view);
        TEST_CHECK(ctx, memcmp(&afterRefusal, &afterMatrix, sizeof(FluxionMat4)) == 0);

        // And a description that names no atlas at all takes the one the
        // view has -- the ordinary case, where a caller fills in a camera
        // and leaves the rest at zero.
        FluxionRenderViewDesc silent = moved;
        silent.shadowAtlasSize = 0;
        silent.shadowTileSize = 0;
        TEST_CHECK(ctx, Fluxion_RenderView_UpdateDescription(view, &silent));
    }

    Fluxion_RenderView_Destroy(view);
    Fluxion_RenderTarget_Destroy(renderTarget);
    Fluxion_RHI_DestroyTextureView(colorView);
    Fluxion_RHI_DestroyTexture(colorTexture);

    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);
}
