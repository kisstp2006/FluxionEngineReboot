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

// GIVES THIS MATERIAL A SECOND WAY TO BE DRAWN: the pass that records
// which way each pixel faces and how rough it is, before anything is lit.
//
// The program must be built from the SAME material source, for
// FLUXION_MATERIAL_PASS_NORMAL_ROUGHNESS. That is the whole point of it
// being a program rather than a shader the engine owns: a material that
// works its surface out unusually is recorded exactly as it will be
// drawn, and the two cannot disagree.
//
// Optional, and a pipeline without one simply does not appear in that
// pass. What reads it -- occlusion and reflections -- then finds nothing
// recorded for those pixels and leaves them alone, which is a picture
// missing an effect rather than a picture that is wrong.
void Fluxion_RenderPipeline_SetPrepassProgram(FluxionRenderPipelineHandle pipeline, FluxionShaderProgramHandle program);

bool Fluxion_RenderPipeline_HasPrepassProgram(FluxionRenderPipelineHandle pipeline);

#ifdef __cplusplus
}
#endif
