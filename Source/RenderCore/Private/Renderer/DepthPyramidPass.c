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

// THE FRAME'S DEPTH, HALVED AGAIN AND AGAIN.
//
// Level zero is the depth this frame drew; each level after it holds the
// FARTHEST depth of the four texels below it. What that buys is an answer
// to "was everything drawn in this rectangle nearer than this" for the
// price of one or two texel reads, whatever the size of the rectangle --
// and that question is the whole of occlusion culling.
//
// DRAWN, NOT DISPATCHED. A compute shader that writes a texture needs a
// storage-image binding and this contract has none; what it has is render
// targets and a way to sample one level of a texture, and that is enough.
// The cost is one small full-screen draw per level -- eleven of them for
// a 1080p frame, each a quarter the size of the one before.

#include "RendererInternal.h"

#include <Fluxion/Foundation/Log.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPass.h>

#include <string.h>

#define FLUXION_PYRAMID_LOG_CATEGORY "Renderer"

static const char* const kPyramidVertexSource = "#include \"Fluxion/Pass/FullscreenVertex.jsl\"\n";
static const char* const kPyramidFragmentSource = "#include \"Fluxion/Pass/DepthPyramid.jsl\"\n";

// Three vertices in clip space, big enough that the triangle covers the
// screen and the corners fall outside it.
static const f32 kFullscreenTriangle[6] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };

// What one level's slice of the uniform buffer holds -- the sizes the
// shader halves between. A whole constant-buffer slice for four numbers,
// because a view into one has to start on a boundary the strictest
// backend chooses, and that boundary is what this stride is.
typedef struct FluxionPyramidUniform
{
    FluxionVec4 sizes;
} FluxionPyramidUniform;

static FluxionRHIBindGroupLayoutDesc FluxionDepthPyramidPass_MakeLayoutDesc(void)
{
    FluxionRHIBindGroupLayoutDesc desc;
    memset(&desc, 0, sizeof(desc));

    desc.entries[0].binding = 0;
    desc.entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    desc.entries[0].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entries[1].binding = 1;
    desc.entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    desc.entries[1].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entries[2].binding = 2;
    desc.entries[2].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    desc.entries[2].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;

    desc.entryCount = 3;
    desc.debugName = "Fluxion.Renderer.DepthPyramidLayout";
    return desc;
}

// How many times a frame of this size can be halved before there is one
// texel left.
static u32 FluxionDepthPyramidPass_LevelCount(u32 width, u32 height)
{
    u32 levels = 1;
    u32 w = width;
    u32 h = height;

    while ((w > 1 || h > 1) && levels < FLUXION_RENDER_HISTORY_MAX_PYRAMID_LEVELS)
    {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        ++levels;
    }
    return levels;
}

static u32 FluxionDepthPyramidPass_LevelSize(u32 size, u32 level)
{
    u32 value = size;
    for (u32 i = 0; i < level; ++i) value = value > 1 ? value / 2 : 1;
    return value;
}

static void FluxionDepthPyramidPass_DestroyBindGroups(FluxionRenderer* renderer)
{
    for (u32 i = 0; i < FLUXION_RENDER_HISTORY_MAX_PYRAMID_LEVELS; ++i)
    {
        if (FLUXION_HANDLE_IS_VALID(renderer->pyramidBindGroups[i])) Fluxion_RHI_DestroyBindGroup(renderer->pyramidBindGroups[i]);
        renderer->pyramidBindGroups[i] = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    }
    renderer->pyramidBoundDepthView = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
}

// How the pyramid is read, by anyone. NEAREST and clamped: every read of
// it names a texel outright, and a filter that blended two would mix
// depths from either side of a silhouette -- a depth that belongs to
// nothing in the scene.
static void FluxionDepthPyramidPass_EnsureSampler(FluxionRenderer* renderer)
{
    if (FLUXION_HANDLE_IS_VALID(renderer->pyramidSampler)) return;

    FluxionRHISamplerDesc samplerDesc;
    memset(&samplerDesc, 0, sizeof(samplerDesc));
    samplerDesc.minFilter = FLUXION_RHI_FILTER_NEAREST;
    samplerDesc.magFilter = FLUXION_RHI_FILTER_NEAREST;
    samplerDesc.mipFilter = FLUXION_RHI_FILTER_NEAREST;
    samplerDesc.addressModeU = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeV = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeW = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;

    // ONE, not zero. A zeroed description says "no anisotropy" nowhere:
    // one is what that means, and zero is a number the contract does not
    // have -- which comes back as no sampler at all, and then as a
    // binding nobody wrote, four steps away.
    samplerDesc.maxAnisotropy = 1.0f;
    samplerDesc.debugName = "Fluxion.Renderer.DepthPyramidSampler";
    renderer->pyramidSampler = Fluxion_RHI_CreateSampler(renderer->device, &samplerDesc);
}

bool FluxionRendererInternal_History_EnsurePyramid(FluxionRenderer* renderer, u32 width, u32 height)
{
    FluxionRenderHistory* history = &renderer->history;

    // MADE WITH THE TEXTURE, not with the pass that fills it: the culling
    // reads the pyramid BEFORE that pass runs, on every frame, and a
    // sampler that did not exist yet is a binding nobody wrote -- which
    // is a dispatch the driver refuses rather than a picture that looks
    // wrong.
    FluxionDepthPyramidPass_EnsureSampler(renderer);

    if (FLUXION_HANDLE_IS_VALID(history->pyramidTexture) && history->width == width && history->height == height) return true;

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
    history->pyramidValid = false;

    // The groups point at views that have just gone.
    FluxionDepthPyramidPass_DestroyBindGroups(renderer);

    const u32 levels = FluxionDepthPyramidPass_LevelCount(width, height);

    FluxionRHITextureDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.width = width;
    desc.height = height;
    desc.depth = 1;
    desc.mipLevels = levels;
    desc.arrayLayers = 1;
    desc.sampleCount = 1;

    // ONE CHANNEL OF FULL-PRECISION FLOAT, not the depth format itself: a
    // depth format is a thing to attach and compare against, and what
    // this holds is an ordinary number that gets sampled, minimised and
    // written like any other.
    desc.format = FLUXION_RHI_FORMAT_R32_FLOAT;
    desc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET | FLUXION_RHI_TEXTURE_USAGE_SAMPLED | FLUXION_RHI_TEXTURE_USAGE_TRANSFER_SRC;
    desc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    desc.debugName = "Fluxion.History.DepthPyramid";

    history->pyramidTexture = Fluxion_RHI_CreateTexture(renderer->device, &desc);
    if (!FLUXION_HANDLE_IS_VALID(history->pyramidTexture))
    {
        FLUXION_LOG_ERROR(FLUXION_PYRAMID_LOG_CATEGORY, "the depth pyramid could not be made; nothing will be culled by what is in front of it");
        return false;
    }

    for (u32 i = 0; i < levels; ++i)
    {
        FluxionRHITextureViewDesc viewDesc;
        memset(&viewDesc, 0, sizeof(viewDesc));
        viewDesc.texture = history->pyramidTexture;
        viewDesc.format = desc.format;
        viewDesc.baseMipLevel = i;
        viewDesc.mipLevelCount = 1;
        viewDesc.baseArrayLayer = 0;
        viewDesc.arrayLayerCount = 1;

        history->pyramidTargetViews[i] = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);
        history->pyramidSampleViews[i] = Fluxion_RHI_CreateTextureView(renderer->device, &viewDesc);

        if (!FLUXION_HANDLE_IS_VALID(history->pyramidTargetViews[i]) || !FLUXION_HANDLE_IS_VALID(history->pyramidSampleViews[i]))
        {
            FLUXION_LOG_ERROR(FLUXION_PYRAMID_LOG_CATEGORY, "a level of the depth pyramid could not be viewed; the pyramid is not usable");
            return false;
        }
    }

    FluxionRHITextureViewDesc wholeDesc;
    memset(&wholeDesc, 0, sizeof(wholeDesc));
    wholeDesc.texture = history->pyramidTexture;
    wholeDesc.format = desc.format;
    wholeDesc.baseMipLevel = 0;
    wholeDesc.mipLevelCount = levels;
    wholeDesc.baseArrayLayer = 0;
    wholeDesc.arrayLayerCount = 1;
    history->pyramidWholeView = Fluxion_RHI_CreateTextureView(renderer->device, &wholeDesc);

    history->pyramidLevels = levels;

    // Made, and in no state at all until something says otherwise -- see
    // pyramidNeedsFirstTransition.
    history->pyramidNeedsFirstTransition = true;
    return true;
}

static bool FluxionDepthPyramidPass_EnsureResources(FluxionRenderer* renderer)
{
    if (renderer->pyramidFailed) return false;
    if (FLUXION_HANDLE_IS_VALID(renderer->pyramidPipeline)) return true;

    FluxionShaderProgramDesc programDesc;
    memset(&programDesc, 0, sizeof(programDesc));
    programDesc.debugName = "Fluxion.Renderer.DepthPyramid";
    programDesc.vertexSource = kPyramidVertexSource;
    programDesc.fragmentSource = kPyramidFragmentSource;

    renderer->pyramidProgram = Fluxion_ShaderProgram_Create(renderer->device, &programDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->pyramidProgram))
    {
        FLUXION_LOG_ERROR(FLUXION_PYRAMID_LOG_CATEGORY, "the depth pyramid shader could not be built; nothing will be culled by what is in front of it");
        renderer->pyramidFailed = true;
        return false;
    }

    const FluxionRHIBindGroupLayoutDesc layoutDesc = FluxionDepthPyramidPass_MakeLayoutDesc();
    renderer->pyramidLayout = Fluxion_RHI_CreateBindGroupLayout(renderer->device, &layoutDesc);

    FluxionDepthPyramidPass_EnsureSampler(renderer);

    FluxionRHIBufferDesc vertexDesc;
    memset(&vertexDesc, 0, sizeof(vertexDesc));
    vertexDesc.size = sizeof(kFullscreenTriangle);
    vertexDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_VERTEX_BUFFER;
    vertexDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    vertexDesc.debugName = "Fluxion.Renderer.FullscreenTriangle";
    renderer->pyramidVertexBuffer = Fluxion_RHI_CreateBuffer(renderer->device, &vertexDesc);

    if (FLUXION_HANDLE_IS_VALID(renderer->pyramidVertexBuffer))
    {
        void* mapped = Fluxion_RHI_MapBuffer(renderer->pyramidVertexBuffer);
        if (mapped != NULL)
        {
            memcpy(mapped, kFullscreenTriangle, sizeof(kFullscreenTriangle));
            Fluxion_RHI_UnmapBuffer(renderer->pyramidVertexBuffer);
        }
    }

    FluxionRHIBufferDesc uniformDesc;
    memset(&uniformDesc, 0, sizeof(uniformDesc));
    uniformDesc.size = (usize)FLUXION_RENDER_HISTORY_MAX_PYRAMID_LEVELS * FLUXION_RENDERER_OBJECT_BUFFER_STRIDE;
    uniformDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER;
    uniformDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    uniformDesc.debugName = "Fluxion.Renderer.DepthPyramidSizes";
    renderer->pyramidUniformBuffer = Fluxion_RHI_CreateBuffer(renderer->device, &uniformDesc);

    FluxionRHIVertexLayout vertexLayout;
    memset(&vertexLayout, 0, sizeof(vertexLayout));
    vertexLayout.attributes[0].location = 0;
    vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    vertexLayout.attributes[0].offset = 0;
    vertexLayout.attributeCount = 1;
    vertexLayout.stride = 2 * sizeof(f32);

    FluxionRHIGraphicsPipelineDesc pipelineDesc;
    memset(&pipelineDesc, 0, sizeof(pipelineDesc));
    pipelineDesc.vertexShader = FluxionRendererInternal_ShaderProgram_GetVertexShader(renderer->pyramidProgram);
    pipelineDesc.fragmentShader = FluxionRendererInternal_ShaderProgram_GetFragmentShader(renderer->pyramidProgram);
    pipelineDesc.vertexLayout = vertexLayout;
    pipelineDesc.rasterState.cullMode = FLUXION_RHI_CULL_MODE_NONE;
    pipelineDesc.rasterState.frontFaceCounterClockwise = false;
    pipelineDesc.rasterState.wireframe = false;
    pipelineDesc.depthState.testEnable = false;
    pipelineDesc.depthState.writeEnable = false;
    pipelineDesc.depthState.compareOp = FLUXION_RHI_COMPARE_OP_ALWAYS;
    pipelineDesc.blendState.mode = FLUXION_RHI_BLEND_MODE_NONE;
    pipelineDesc.topology = FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineDesc.colorFormats[0] = FLUXION_RHI_FORMAT_R32_FLOAT;
    pipelineDesc.colorFormatCount = 1;
    pipelineDesc.depthFormat = FLUXION_RHI_FORMAT_UNKNOWN;

    const FluxionRHIBindGroupLayoutHandle noLayout = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_GLOBAL] = renderer->pyramidLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_FRAME] = noLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_MATERIAL] = noLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_OBJECT] = noLayout;
    pipelineDesc.bindGroupLayoutCount = FLUXION_RHI_BIND_GROUP_GLOBAL + 1;
    pipelineDesc.debugName = "Fluxion.Renderer.DepthPyramidPipeline";

    renderer->pyramidPipeline = Fluxion_RHI_CreateGraphicsPipeline(renderer->device, &pipelineDesc);
    if (!FLUXION_HANDLE_IS_VALID(renderer->pyramidPipeline) || !FLUXION_HANDLE_IS_VALID(renderer->pyramidVertexBuffer) ||
        !FLUXION_HANDLE_IS_VALID(renderer->pyramidUniformBuffer) || !FLUXION_HANDLE_IS_VALID(renderer->pyramidSampler))
    {
        FLUXION_LOG_ERROR(FLUXION_PYRAMID_LOG_CATEGORY, "the depth pyramid pass could not be set up; nothing will be culled by what is in front of it");
        renderer->pyramidFailed = true;
        return false;
    }
    return true;
}

// One bind group per level: the sizes it halves between, and the level it
// reads. Built once and kept -- what changes from frame to frame is what
// those textures HOLD, not which they are.
static bool FluxionDepthPyramidPass_EnsureBindGroups(FluxionRenderer* renderer, FluxionRHITextureViewHandle depthView)
{
    const FluxionRenderHistory* history = &renderer->history;

    if (renderer->pyramidBoundDepthView.index == depthView.index && renderer->pyramidBoundDepthView.generation == depthView.generation &&
        FLUXION_HANDLE_IS_VALID(renderer->pyramidBindGroups[0]))
    {
        return true;
    }

    FluxionDepthPyramidPass_DestroyBindGroups(renderer);

    u8* uniforms = (u8*)Fluxion_RHI_MapBuffer(renderer->pyramidUniformBuffer);
    if (uniforms == NULL) return false;

    for (u32 level = 0; level < history->pyramidLevels; ++level)
    {
        // Level zero reads the frame's own depth; every level after it
        // reads the one before.
        const FluxionRHITextureViewHandle source = level == 0 ? depthView : history->pyramidSampleViews[level - 1];

        const u32 sourceWidth = level == 0 ? history->width : FluxionDepthPyramidPass_LevelSize(history->width, level - 1);
        const u32 sourceHeight = level == 0 ? history->height : FluxionDepthPyramidPass_LevelSize(history->height, level - 1);

        FluxionPyramidUniform sizes;
        sizes.sizes.x = (f32)sourceWidth;
        sizes.sizes.y = (f32)sourceHeight;
        sizes.sizes.z = (f32)FluxionDepthPyramidPass_LevelSize(history->width, level);
        sizes.sizes.w = (f32)FluxionDepthPyramidPass_LevelSize(history->height, level);
        memcpy(uniforms + (usize)level * FLUXION_RENDERER_OBJECT_BUFFER_STRIDE, &sizes, sizeof(sizes));

        FluxionRHIBindGroupEntry entries[3];
        memset(entries, 0, sizeof(entries));

        entries[0].binding = 0;
        entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
        entries[0].buffer = renderer->pyramidUniformBuffer;
        entries[0].bufferOffset = (usize)level * FLUXION_RENDERER_OBJECT_BUFFER_STRIDE;
        entries[0].bufferSize = sizeof(FluxionPyramidUniform);

        entries[1].binding = 1;
        entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
        entries[1].textureView = source;

        entries[2].binding = 2;
        entries[2].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
        entries[2].sampler = renderer->pyramidSampler;

        FluxionRHIBindGroupDesc groupDesc;
        groupDesc.layout = renderer->pyramidLayout;
        groupDesc.entries = entries;
        groupDesc.entryCount = 3;

        renderer->pyramidBindGroups[level] = Fluxion_RHI_CreateBindGroup(renderer->device, &groupDesc);
        if (!FLUXION_HANDLE_IS_VALID(renderer->pyramidBindGroups[level]))
        {
            Fluxion_RHI_UnmapBuffer(renderer->pyramidUniformBuffer);
            FluxionDepthPyramidPass_DestroyBindGroups(renderer);
            return false;
        }
    }

    Fluxion_RHI_UnmapBuffer(renderer->pyramidUniformBuffer);
    renderer->pyramidBoundDepthView = depthView;
    return true;
}

void FluxionDepthPyramidPass_Setup(FluxionRenderGraphBuilder* builder, void* userData)
{
    FLUXION_UNUSED(userData);

    // THE DEPTH IS DECLARED AS A TARGET, THEN BORROWED.
    //
    // What this pass really does is SAMPLE the depth, and saying so here
    // would be simpler -- but the graph would then leave it in a state a
    // shader reads, and the frame is not over when the graph is: whatever
    // draws after it (the debug lines, for one) attaches the depth again
    // and finds it in the wrong state.
    //
    // So the pass declares what it is ORDERED by (a write, which puts it
    // after everything that fills the depth) and moves the texture itself
    // for exactly as long as it reads it -- see Execute. What the graph
    // believes about the state is then true at the end of the pass, which
    // is the only moment anything else looks.
    Fluxion_RenderGraphBuilder_WriteDepthTarget(builder, "ForwardOpaquePass.Depth");
    Fluxion_RenderGraphBuilder_WriteColorTarget(builder, "DepthPyramidPass.Pyramid");
}

void FluxionDepthPyramidPass_Execute(FluxionRHICommandListHandle commandList, void* userData)
{
    FluxionRenderer* renderer = (FluxionRenderer*)userData;
    if (renderer == NULL) return;

    FluxionRenderHistory* history = &renderer->history;
    if (!FLUXION_HANDLE_IS_VALID(history->pyramidTexture)) return;
    if (!FluxionDepthPyramidPass_EnsureResources(renderer)) return;

    FluxionRenderTargetHandle renderTarget = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRendererInternal_RenderView_Get(renderer->currentView, &renderTarget, NULL, NULL);

    FluxionRHITextureViewHandle colorViews[FLUXION_RENDER_TARGET_MAX_COLOR_ATTACHMENTS];
    u32 colorViewCount = 0;
    FluxionRHITextureViewHandle depthView = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (!FluxionRendererInternal_RenderTarget_Get(renderTarget, colorViews, &colorViewCount, &depthView)) return;
    if (!FLUXION_HANDLE_IS_VALID(depthView)) return;

    if (!FluxionDepthPyramidPass_EnsureBindGroups(renderer, depthView)) return;

    const FluxionRHITextureHandle noTexture = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    // Borrowed: readable while the first level is built, and handed back
    // the way it was found -- see Setup. The texture behind the view,
    // because a barrier is about a texture and a render target is a set
    // of views.
    const FluxionRHITextureHandle depthTexture = Fluxion_RHI_GetTextureViewTexture(depthView);
    if (!FLUXION_HANDLE_IS_VALID(depthTexture)) return;

    FluxionRHIBarrier depthToRead = { depthTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE,
                                      FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(commandList, &depthToRead, 1);

    for (u32 level = 0; level < history->pyramidLevels; ++level)
    {
        // The level being written, made attachable; and the one before
        // it, made readable. Both are the same texture, which is why they
        // are separate levels rather than separate moments: a barrier per
        // level is what keeps the write of one from racing the read of
        // the next.
        if (level > 0)
        {
            // ONE LEVEL, NOT THE TEXTURE. The level being written and the
            // one being read are the same texture in two different states
            // at the same moment, which is the whole reason a barrier can
            // name a range of levels at all.
            FluxionRHIBarrier previousToRead = { history->pyramidTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET,
                                                 FLUXION_RHI_RESOURCE_STATE_SHADER_READ, level - 1, 1 };
            Fluxion_RHI_CommandList_Barrier(commandList, &previousToRead, 1);
        }

        FluxionRHIRenderingAttachment attachment;
        attachment.view = history->pyramidTargetViews[level];
        attachment.clear = false;
        attachment.clearColor[0] = 1.0f;
        attachment.clearColor[1] = 1.0f;
        attachment.clearColor[2] = 1.0f;
        attachment.clearColor[3] = 1.0f;

        const u32 levelWidth = FluxionDepthPyramidPass_LevelSize(history->width, level);
        const u32 levelHeight = FluxionDepthPyramidPass_LevelSize(history->height, level);

        FluxionRHIRenderingDesc renderingDesc;
        renderingDesc.colorAttachments = &attachment;
        renderingDesc.colorAttachmentCount = 1;
        renderingDesc.depthAttachment = NULL;
        renderingDesc.width = levelWidth;
        renderingDesc.height = levelHeight;

        Fluxion_RHI_CommandList_BeginRendering(commandList, &renderingDesc);
        Fluxion_RHI_CommandList_SetViewport(commandList, 0.0f, 0.0f, (f32)levelWidth, (f32)levelHeight, 0.0f, 1.0f);
        Fluxion_RHI_CommandList_SetScissor(commandList, 0, 0, levelWidth, levelHeight);
        Fluxion_RHI_CommandList_SetPipeline(commandList, renderer->pyramidPipeline);
        Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_GLOBAL, renderer->pyramidBindGroups[level]);
        Fluxion_RHI_CommandList_SetVertexBuffer(commandList, 0, renderer->pyramidVertexBuffer, 0);
        Fluxion_RHI_CommandList_Draw(commandList, 3, 1, 0, 0);
        Fluxion_RHI_CommandList_EndRendering(commandList);
    }

    // And the last level, which nothing has read yet, so that the whole
    // pyramid is readable by whoever asks it what is in front of what.
    FluxionRHIBarrier toRead = { history->pyramidTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_RENDER_TARGET,
                                 FLUXION_RHI_RESOURCE_STATE_SHADER_READ, history->pyramidLevels - 1, 1 };
    Fluxion_RHI_CommandList_Barrier(commandList, &toRead, 1);

    // And the depth back where it was found.
    FluxionRHIBarrier depthBack = { depthTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_SHADER_READ,
                                    FLUXION_RHI_RESOURCE_STATE_DEPTH_WRITE, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(commandList, &depthBack, 1);
    FLUXION_UNUSED(noTexture);

    // What it holds is this frame -- which is what makes it the PREVIOUS
    // frame for the one after this.
    history->pyramidValid = true;
}

void FluxionRendererInternal_DepthPyramid_Destroy(FluxionRenderer* renderer)
{
    if (renderer == NULL) return;

    FluxionDepthPyramidPass_DestroyBindGroups(renderer);

    if (FLUXION_HANDLE_IS_VALID(renderer->pyramidPipeline)) Fluxion_RHI_DestroyPipeline(renderer->pyramidPipeline);
    if (FLUXION_HANDLE_IS_VALID(renderer->pyramidLayout)) Fluxion_RHI_DestroyBindGroupLayout(renderer->pyramidLayout);
    if (FLUXION_HANDLE_IS_VALID(renderer->pyramidSampler)) Fluxion_RHI_DestroySampler(renderer->pyramidSampler);
    if (FLUXION_HANDLE_IS_VALID(renderer->pyramidVertexBuffer)) Fluxion_RHI_DestroyBuffer(renderer->pyramidVertexBuffer);
    if (FLUXION_HANDLE_IS_VALID(renderer->pyramidUniformBuffer)) Fluxion_RHI_DestroyBuffer(renderer->pyramidUniformBuffer);
    if (FLUXION_HANDLE_IS_VALID(renderer->pyramidProgram)) Fluxion_ShaderProgram_Destroy(renderer->pyramidProgram);

    renderer->pyramidPipeline = (FluxionRHIPipelineHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->pyramidLayout = (FluxionRHIBindGroupLayoutHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->pyramidSampler = (FluxionRHISamplerHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->pyramidVertexBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->pyramidUniformBuffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    renderer->pyramidProgram = (FluxionShaderProgramHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
}
