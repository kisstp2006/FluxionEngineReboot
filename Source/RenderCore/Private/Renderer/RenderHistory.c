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


#include "RendererInternal.h"

#include <Fluxion/Foundation/Log.h>

#include <string.h>

// WHAT SURVIVES FROM ONE FRAME TO THE NEXT.
//
// A frame that wants to know where a pixel was, or what could be seen a
// moment ago, has to read something the frame before it left behind. This
// file owns those textures and the one question that matters about them:
// whether what they hold belongs to the frame just gone, or to nothing.
//
// It is the RENDERER'S and not the view's -- see FluxionRenderHistory --
// and it is made lazily, at the size of the first frame that asks.

#define FLUXION_RENDER_HISTORY_LOG_CATEGORY "RenderHistory"

static void FluxionRendererInternal_History_DestroyTextures(FluxionRenderer* renderer)
{
    FluxionRenderHistory* history = &renderer->history;

    if (FLUXION_HANDLE_IS_VALID(history->motionView)) Fluxion_RHI_DestroyTextureView(history->motionView);
    if (FLUXION_HANDLE_IS_VALID(history->motionTexture)) Fluxion_RHI_DestroyTexture(history->motionTexture);

    history->motionView = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    history->motionTexture = (FluxionRHITextureHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    for (u32 i = 0; i < FLUXION_RENDER_HISTORY_MAX_PYRAMID_LEVELS; ++i)
    {
        if (FLUXION_HANDLE_IS_VALID(history->pyramidTargetViews[i])) Fluxion_RHI_DestroyTextureView(history->pyramidTargetViews[i]);
        if (FLUXION_HANDLE_IS_VALID(history->pyramidSampleViews[i])) Fluxion_RHI_DestroyTextureView(history->pyramidSampleViews[i]);
        history->pyramidTargetViews[i] = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
        history->pyramidSampleViews[i] = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }
    if (FLUXION_HANDLE_IS_VALID(history->pyramidWholeView)) Fluxion_RHI_DestroyTextureView(history->pyramidWholeView);
    history->pyramidWholeView = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (FLUXION_HANDLE_IS_VALID(history->pyramidTexture)) Fluxion_RHI_DestroyTexture(history->pyramidTexture);
    history->pyramidTexture = (FluxionRHITextureHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    history->pyramidLevels = 0;
    history->pyramidValid = false;

    history->width = 0;
    history->height = 0;
}

bool FluxionRendererInternal_History_Begin(FluxionRenderer* renderer, u32 width, u32 height)
{
    if (renderer == NULL || width == 0 || height == 0) return false;

    FluxionRenderHistory* history = &renderer->history;

    if (history->width == width && history->height == height && FLUXION_HANDLE_IS_VALID(history->motionTexture)) return true;

    // A different size, so what was kept describes a screen that no
    // longer exists. Remade, and the history is not readable this frame
    // -- the frame after this one is the first that has anything behind
    // it again.
    FluxionRendererInternal_History_DestroyTextures(renderer);
    history->valid = false;

    FluxionRHITextureDesc motionDesc;
    memset(&motionDesc, 0, sizeof(motionDesc));
    motionDesc.width = width;
    motionDesc.height = height;
    motionDesc.depth = 1;
    motionDesc.mipLevels = 1;
    motionDesc.arrayLayers = 1;
    motionDesc.sampleCount = 1;

    // TWO SIGNED, FLOATING-POINT CHANNELS. A motion vector is a fraction
    // of the screen, so its useful range is about -1 to 1 and its
    // precision matters most near zero -- which is what a float format
    // gives and a normalised integer one does not.
    //
    // FULL precision, at eight bytes a pixel, because the RHI's format
    // list has no two-channel half format yet. Half would do here and
    // would halve this; adding a format is four backends' worth of
    // tables, and it is written down rather than quietly paid for.
    motionDesc.format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    motionDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET | FLUXION_RHI_TEXTURE_USAGE_SAMPLED |
                            FLUXION_RHI_TEXTURE_USAGE_TRANSFER_SRC;
    motionDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    motionDesc.debugName = "Fluxion.History.MotionVectors";

    history->motionTexture = Fluxion_RHI_CreateTexture(renderer->device, &motionDesc);
    if (!FLUXION_HANDLE_IS_VALID(history->motionTexture))
    {
        FLUXION_LOG_ERROR(FLUXION_RENDER_HISTORY_LOG_CATEGORY, "the motion vector target could not be made; nothing temporal will work this run");
        return false;
    }

    FluxionRHITextureViewDesc viewDesc;
    memset(&viewDesc, 0, sizeof(viewDesc));
    viewDesc.texture = history->motionTexture;
    viewDesc.format = motionDesc.format;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;

    history->motionView = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);
    if (!FLUXION_HANDLE_IS_VALID(history->motionView))
    {
        FluxionRendererInternal_History_DestroyTextures(renderer);
        return false;
    }

    history->width = width;
    history->height = height;

    // The pyramid is made here too, because it is the same answer to the
    // same question: what a frame of this size needs kept.
    if (!FluxionRendererInternal_History_EnsurePyramid(renderer, width, height)) return false;

    return true;
}

void FluxionRendererInternal_History_Destroy(FluxionRenderer* renderer)
{
    if (renderer == NULL) return;

    FluxionRendererInternal_History_DestroyTextures(renderer);
    renderer->history.valid = false;
    renderer->history.hasPreviousViewProjection = false;
}
