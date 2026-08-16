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

// What is behind everything else.
//
// A sky is not a material and not a surface. No material declares it,
// nothing lights it, and it has no place in the world -- so it does not
// go through the draw packets at all. It is one triangle covering the
// screen, drawn where nothing else was, sampling the environment map in
// the direction the eye is looking.
//
// It reads the SAME environment the lighting will read for its
// reflections. That is the whole reason it is worth drawing: a sky from
// one source beside reflections from another agree only by accident, and
// the disagreement reads as a reflection bug rather than as two different
// pictures.

#include "RendererInternal.h"

#include <Fluxion/Foundation/Log.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>

#include <cstring>

namespace
{

// The engine's own library supplies both halves, so the sky goes through
// the same include resolver, the same shader cache and the same exposure
// and tone mapping as everything else. Written out as two include lines
// rather than as shader text: the moment the text lived here it would
// stop being the file the cache watches.
const char* const kSkyboxVertexSource = "#include \"Fluxion/Pass/SkyboxVertex.jsl\"\n";
const char* const kSkyboxFragmentSource = "#include \"Fluxion/Pass/Skybox.jsl\"\n";

// Three vertices, not four. A single triangle large enough to cover the
// screen has no seam down the middle where two would meet, and rasterizes
// as one piece.
struct SkyboxVertex
{
    f32 position[2];
};

const SkyboxVertex kSkyboxVertices[3] = {
    { { -1.0f, -1.0f } },
    { { 3.0f, -1.0f } },
    { { -1.0f, 3.0f } },
};

} // namespace

extern "C" bool FluxionRendererInternal_Skybox_EnsureResources(FluxionRenderer* renderer)
{
    if (FLUXION_HANDLE_IS_VALID(renderer->skyboxProgram)) return true;
    if (renderer->skyboxFailed) return false;

    FluxionShaderProgramDesc programDesc;
    std::memset(&programDesc, 0, sizeof(programDesc));
    programDesc.debugName = "Fluxion.Renderer.Skybox";
    programDesc.vertexSource = kSkyboxVertexSource;
    programDesc.fragmentSource = kSkyboxFragmentSource;

    renderer->skyboxProgram = Fluxion_ShaderProgram_Create(renderer->device, &programDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->skyboxProgram))
    {
        // Remembered, so the attempt is made once rather than once a
        // frame. A shader that failed to build will fail again, and
        // saying so sixty times a second buries whatever said it first.
        FLUXION_LOG_ERROR("Renderer", "The sky's shaders could not be built; nothing will be drawn behind the scene.");
        renderer->skyboxFailed = true;
        return false;
    }

    FluxionRHIBufferDesc vertexBufferDesc;
    vertexBufferDesc.size = sizeof(kSkyboxVertices);
    vertexBufferDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_VERTEX_BUFFER;
    vertexBufferDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    vertexBufferDesc.debugName = "Fluxion.Renderer.Skybox.VertexBuffer";
    renderer->skyboxVertexBuffer = Fluxion_RHI_CreateBuffer(renderer->device, &vertexBufferDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->skyboxVertexBuffer))
    {
        renderer->skyboxFailed = true;
        return false;
    }

    void* mapped = Fluxion_RHI_MapBuffer(renderer->skyboxVertexBuffer);
    if (mapped == nullptr)
    {
        renderer->skyboxFailed = true;
        return false;
    }
    std::memcpy(mapped, kSkyboxVertices, sizeof(kSkyboxVertices));
    Fluxion_RHI_UnmapBuffer(renderer->skyboxVertexBuffer);

    return true;
}

extern "C" void FluxionRendererInternal_Skybox_Draw(FluxionRenderer* renderer, FluxionRHICommandListHandle commandList,
                                         FluxionRHIBindGroupHandle frameBindGroup,
                                         FluxionRHIFormat colorFormat, FluxionRHIFormat depthFormat, bool hasDepthAttachment)
{
    // The formats must be known and nothing here can find them out -- a
    // texture view carries no queryable format, so the only source is
    // what the caller said. A mismatch surfaces at the draw as two
    // formats with no hint that one was never set; refusing here, once,
    // by name, is the better answer. The pass says whether there IS a
    // depth attachment; only the caller can have said its format.
    const bool colorUnknown = colorFormat == FLUXION_RHI_FORMAT_UNKNOWN;
    const bool depthUnknown = hasDepthAttachment && depthFormat == FLUXION_RHI_FORMAT_UNKNOWN;
    if (colorUnknown || depthUnknown)
    {
        if (!renderer->skyboxFormatsReported)
        {
            FLUXION_LOG_WARN("Renderer",
                "No sky: this renderer has not been told which %s format it draws into.",
                colorUnknown ? "colour" : "depth");
            renderer->skyboxFormatsReported = true;
        }
        return;
    }

    if (!FluxionRendererInternal_Skybox_EnsureResources(renderer)) return;

    // Built against the formats this frame actually draws into, and
    // rebuilt when they change. A texture view carries no queryable
    // format, so the pass that owns the attachments is the only thing
    // that can say what they are.
    if (!FLUXION_HANDLE_IS_VALID(renderer->skyboxPipeline) ||
        renderer->skyboxColorFormat != colorFormat || renderer->skyboxDepthFormat != depthFormat)
    {
        if (FLUXION_HANDLE_IS_VALID(renderer->skyboxPipeline)) Fluxion_RHI_DestroyPipeline(renderer->skyboxPipeline);
        if (FLUXION_HANDLE_IS_VALID(renderer->skyboxFrameLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->skyboxFrameLayout);

        FluxionRHIVertexLayout vertexLayout{};
        vertexLayout.attributes[0].location = 0;
        vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
        vertexLayout.attributes[0].offset = 0;
        vertexLayout.attributeCount = 1;
        vertexLayout.stride = sizeof(SkyboxVertex);

        renderer->skyboxPipeline = FluxionRendererInternal_ShaderProgram_CreateSkyboxPipeline(
            renderer->device, renderer->skyboxProgram, &vertexLayout, colorFormat, depthFormat, &renderer->skyboxFrameLayout);

        renderer->skyboxColorFormat = colorFormat;
        renderer->skyboxDepthFormat = depthFormat;
    }

    if (!FLUXION_HANDLE_IS_VALID(renderer->skyboxPipeline)) return;

    Fluxion_RHI_CommandList_SetPipeline(commandList, renderer->skyboxPipeline);

    // After SetPipeline, never before: one backend needs a pipeline bound
    // to know which layout a bind group belongs to, and silently does
    // nothing otherwise.
    if (FLUXION_HANDLE_IS_VALID(frameBindGroup))
    {
        Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_FRAME, frameBindGroup);
    }

    Fluxion_RHI_CommandList_SetVertexBuffer(commandList, 0, renderer->skyboxVertexBuffer, 0);
    Fluxion_RHI_CommandList_Draw(commandList, 3, 1, 0, 0);
    ++renderer->lastDrawCallCount;
}

extern "C" void FluxionRendererInternal_Skybox_Destroy(FluxionRenderer* renderer)
{
    if (FLUXION_HANDLE_IS_VALID(renderer->skyboxPipeline)) Fluxion_RHI_DestroyPipeline(renderer->skyboxPipeline);
    if (FLUXION_HANDLE_IS_VALID(renderer->skyboxFrameLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->skyboxFrameLayout);
    if (FLUXION_HANDLE_IS_VALID(renderer->skyboxVertexBuffer)) Fluxion_RHI_DestroyBuffer(renderer->skyboxVertexBuffer);
    if (FLUXION_HANDLE_IS_VALID(renderer->skyboxProgram)) Fluxion_ShaderProgram_Destroy(renderer->skyboxProgram);

    renderer->skyboxPipeline = FluxionRHIPipelineHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->skyboxFrameLayout = FluxionRHIBindGroupLayoutHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->skyboxVertexBuffer = FluxionRHIBufferHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->skyboxProgram = FluxionShaderProgramHandle{ FLUXION_HANDLE_INVALID_INDEX, 0 };
}
