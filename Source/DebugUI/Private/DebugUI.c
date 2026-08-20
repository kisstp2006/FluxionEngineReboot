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

// THE TOOLKIT LIVES HERE AND NOWHERE ELSE.
//
// Everything above this file speaks in panels and sliders; this is the
// one place that knows those are Nuklear's. The whole point of the API
// beside it is that this file could be replaced -- the switches, the
// atlas, the vertex format and the command list below are all local
// decisions, not promises to anybody.
//
// WHAT IT TAKES TO DRAW ONE: the toolkit hands back a list of triangles
// in pixel coordinates, an index buffer, and a run of commands that each
// name a scissor rectangle and a texture. The atlas is baked once, at
// startup, from the font built into the library -- so a developer panel
// needs no asset shipped beside it and works in a test as readily as in
// a sample.

#include <Fluxion/DebugUI/DebugUI.h>

#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/RenderCore/Renderer/ShaderProgram.h>

// THE TOOLKIT'S OWN HEADER IS NOT OUR CODE. It is C89 with unions,
// implicit conversions and unused parameters throughout, and it compiles
// clean for what it is -- but not against warnings chosen for this
// engine's own files.
#if defined(_MSC_VER)
#    pragma warning(push, 0)
#elif defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wall"
#    pragma GCC diagnostic ignored "-Wextra"
#    pragma GCC diagnostic ignored "-Wconversion"
#    pragma GCC diagnostic ignored "-Wsign-conversion"
#    pragma GCC diagnostic ignored "-Wswitch-default"
#    pragma GCC diagnostic ignored "-Wunused-parameter"
#    pragma GCC diagnostic ignored "-Wunused-function"
#    pragma GCC diagnostic ignored "-Wfloat-equal"
#    pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include <nuklear.h>

#if defined(_MSC_VER)
#    pragma warning(pop)
#elif defined(__clang__)
#    pragma clang diagnostic pop
#elif defined(__GNUC__)
#    pragma GCC diagnostic pop
#endif

#include <stddef.h>
#include <string.h>

#define FLUXION_DEBUG_UI_LOG_CATEGORY "DebugUI"

// How much geometry one frame of panels may come to. A developer panel is
// a few hundred rectangles; this is room for thousands, and running out
// is reported rather than drawn half.
#define FLUXION_DEBUG_UI_VERTEX_BYTES (512u * 1024u)
#define FLUXION_DEBUG_UI_INDEX_BYTES (128u * 1024u)

// The screen's size, as one four-component value -- the smallest thing a
// uniform buffer holds in either language.
#define FLUXION_DEBUG_UI_UNIFORM_BYTES 16u

// What the toolkit's own bookkeeping runs in: one block, taken once, and
// never grown. A panel that runs out of it is reported rather than
// allowed to reach for more in the middle of a frame.
#define FLUXION_DEBUG_UI_CONTEXT_BYTES (256u * 1024u)

static const char* const kDebugUIVertexSource = "#include \"Fluxion/Pass/DebugUIVertex.jsl\"\n";
static const char* const kDebugUIFragmentSource = "#include \"Fluxion/Pass/DebugUI.jsl\"\n";

// Position, texture coordinate and colour, all as floats. The colour is
// four bytes in the toolkit's own layout and could be given as such --
// floats instead because a four-byte normalised vertex format is one more
// thing every backend would have to agree about, for four bytes a vertex
// in geometry that is measured in kilobytes.
typedef struct FluxionDebugUIVertex
{
    f32 position[2];
    f32 uv[2];
    f32 color[4];
} FluxionDebugUIVertex;

typedef struct FluxionDebugUIState
{
    bool ready;

    FluxionRHIDeviceHandle device;
    FluxionRHIQueueHandle queue;
    FluxionRHIFormat colorFormat;

    struct nk_context context;
    struct nk_font_atlas atlas;
    struct nk_buffer commands;
    struct nk_draw_null_texture nullTexture;

    void* contextMemory;

    FluxionRHITextureHandle atlasTexture;
    FluxionRHITextureViewHandle atlasView;
    FluxionRHISamplerHandle sampler;

    FluxionShaderProgramHandle program;
    FluxionRHIPipelineHandle pipeline;
    FluxionRHIBindGroupLayoutHandle layout;
    FluxionRHIBindGroupHandle bindGroup;
    FluxionRHIBufferHandle uniformBuffer;
    FluxionRHIBufferHandle vertexBuffer;
    FluxionRHIBufferHandle indexBuffer;

    u32 frameWidth;
    u32 frameHeight;
    bool inFrame;
} FluxionDebugUIState;

static FluxionDebugUIState s_ui;

// THE TOOLKIT ALLOCATES THROUGH THIS ENGINE, not through the standard
// library. Its own default allocator is deliberately not switched on: a
// panel toolkit reaching straight for malloc is memory this engine cannot
// see, and being able to see it is most of the point of having an
// allocator at all.
//
// The size has to be carried, because the toolkit's free is given only a
// pointer while this engine's wants the size back. One header per block,
// which is what every allocator that answers "free(ptr)" does somewhere.
typedef struct FluxionDebugUIBlockHeader
{
    usize size;
    usize padding; // keeps what follows on a sixteen-byte boundary
} FluxionDebugUIBlockHeader;

static void* FluxionDebugUI_Alloc(nk_handle handle, void* old, nk_size size)
{
    (void)handle;
    (void)old; // the toolkit never asks this to grow a block in place

    const usize total = sizeof(FluxionDebugUIBlockHeader) + (usize)size;
    u8* block = (u8*)Fluxion_Allocator_Alloc(Fluxion_DefaultAllocator(), total, FLUXION_DEFAULT_ALIGNMENT);
    if (block == NULL) return NULL;

    ((FluxionDebugUIBlockHeader*)block)->size = total;
    return block + sizeof(FluxionDebugUIBlockHeader);
}

static void FluxionDebugUI_Free(nk_handle handle, void* pointer)
{
    (void)handle;
    if (pointer == NULL) return;

    u8* block = (u8*)pointer - sizeof(FluxionDebugUIBlockHeader);
    const usize total = ((const FluxionDebugUIBlockHeader*)block)->size;
    Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), block, total);
}

static struct nk_allocator FluxionDebugUI_Allocator(void)
{
    struct nk_allocator allocator;
    memset(&allocator, 0, sizeof(allocator));
    allocator.alloc = FluxionDebugUI_Alloc;
    allocator.free = FluxionDebugUI_Free;
    return allocator;
}

// ---------------------------------------------------------------------
// The atlas.
// ---------------------------------------------------------------------

static bool FluxionDebugUI_BakeFont(void)
{
    struct nk_allocator allocator = FluxionDebugUI_Allocator();
    nk_font_atlas_init(&s_ui.atlas, &allocator);
    nk_font_atlas_begin(&s_ui.atlas);

    int width = 0;
    int height = 0;
    const void* pixels = nk_font_atlas_bake(&s_ui.atlas, &width, &height, NK_FONT_ATLAS_RGBA32);
    if (pixels == NULL || width <= 0 || height <= 0)
    {
        FLUXION_LOG_ERROR(FLUXION_DEBUG_UI_LOG_CATEGORY, "the panel font could not be baked; there will be no panels");
        return false;
    }

    FluxionRHITextureDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.width = (u32)width;
    desc.height = (u32)height;
    desc.depth = 1;
    desc.mipLevels = 1;
    desc.arrayLayers = 1;
    desc.sampleCount = 1;
    desc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;
    desc.format = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    desc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_SAMPLED | FLUXION_RHI_TEXTURE_USAGE_TRANSFER_DST;
    desc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    desc.debugName = "Fluxion.DebugUI.Atlas";

    s_ui.atlasTexture = Fluxion_RHI_CreateTexture(s_ui.device, &desc);
    if (!FLUXION_HANDLE_IS_VALID(s_ui.atlasTexture)) return false;

    const usize rowBytes = (usize)width * 4u;
    const usize alignedRowBytes = (rowBytes + FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT - 1) /
                                  FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT * FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT;

    FluxionRHIBufferDesc stagingDesc;
    memset(&stagingDesc, 0, sizeof(stagingDesc));
    stagingDesc.size = alignedRowBytes * (usize)height;
    stagingDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC;
    stagingDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    stagingDesc.debugName = "Fluxion.DebugUI.AtlasUpload";
    FluxionRHIBufferHandle staging = Fluxion_RHI_CreateBuffer(s_ui.device, &stagingDesc);
    if (!FLUXION_HANDLE_IS_VALID(staging)) return false;

    // ROW BY ROW, because the rows are spaced out on the way up by the
    // alignment this contract names and the baked image is packed tight.
    u8* mapped = (u8*)Fluxion_RHI_MapBuffer(staging);
    if (mapped != NULL)
    {
        for (u32 y = 0; y < (u32)height; ++y)
        {
            memcpy(mapped + (usize)y * alignedRowBytes, (const u8*)pixels + (usize)y * rowBytes, rowBytes);
        }
        Fluxion_RHI_UnmapBuffer(staging);
    }

    FluxionRHICommandListHandle upload = Fluxion_RHI_CreateCommandList(s_ui.device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RHI_CommandList_Begin(upload);

    const FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    FluxionRHIBarrier toCopy = { s_ui.atlasTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_UNDEFINED,
                                 FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(upload, &toCopy, 1);
    Fluxion_RHI_CommandList_CopyBufferToTexture(upload, staging, 0, s_ui.atlasTexture, 0, 0);
    FluxionRHIBarrier toRead = { s_ui.atlasTexture, noBuffer, FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION,
                                 FLUXION_RHI_RESOURCE_STATE_SHADER_READ, 0, 0 };
    Fluxion_RHI_CommandList_Barrier(upload, &toRead, 1);

    Fluxion_RHI_CommandList_End(upload);

    FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(s_ui.device, false);
    Fluxion_RHI_Queue_Submit(s_ui.queue, &upload, 1, fence);
    Fluxion_RHI_WaitForFence(fence);
    Fluxion_RHI_DestroyFence(fence);
    Fluxion_RHI_DestroyCommandList(upload);
    Fluxion_RHI_DestroyBuffer(staging);

    FluxionRHITextureViewDesc viewDesc;
    memset(&viewDesc, 0, sizeof(viewDesc));
    viewDesc.texture = s_ui.atlasTexture;
    viewDesc.format = desc.format;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    viewDesc.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;
    s_ui.atlasView = Fluxion_RHI_CreateTextureView(s_ui.device, &viewDesc);

    // The toolkit is told what it baked into, and hands back the one
    // texture coordinate that reads as solid white -- which is what every
    // rectangle that is not a glyph is drawn with.
    nk_font_atlas_end(&s_ui.atlas, nk_handle_id(0), &s_ui.nullTexture);

    return FLUXION_HANDLE_IS_VALID(s_ui.atlasView);
}

// ---------------------------------------------------------------------
// What draws it.
// ---------------------------------------------------------------------

static bool FluxionDebugUI_BuildPipeline(void)
{
    FluxionShaderProgramDesc programDesc;
    memset(&programDesc, 0, sizeof(programDesc));
    programDesc.debugName = "Fluxion.DebugUI";
    programDesc.vertexSource = kDebugUIVertexSource;
    programDesc.fragmentSource = kDebugUIFragmentSource;

    s_ui.program = Fluxion_ShaderProgram_Create(s_ui.device, &programDesc);
    if (!FLUXION_HANDLE_IS_VALID(s_ui.program))
    {
        FLUXION_LOG_ERROR(FLUXION_DEBUG_UI_LOG_CATEGORY, "the panel shader could not be built; there will be no panels");
        return false;
    }

    FluxionRHIBindGroupLayoutDesc layoutDesc;
    memset(&layoutDesc, 0, sizeof(layoutDesc));
    layoutDesc.entries[0].binding = 0;
    layoutDesc.entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    layoutDesc.entries[0].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_VERTEX | FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;
    layoutDesc.entries[1].binding = 1;
    layoutDesc.entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    layoutDesc.entries[1].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;
    layoutDesc.entries[2].binding = 2;
    layoutDesc.entries[2].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    layoutDesc.entries[2].visibility = FLUXION_RHI_SHADER_STAGE_FLAG_FRAGMENT;
    layoutDesc.entryCount = 3;
    layoutDesc.debugName = "Fluxion.DebugUI.Layout";
    s_ui.layout = Fluxion_RHI_CreateBindGroupLayout(s_ui.device, &layoutDesc);

    FluxionRHISamplerDesc samplerDesc;
    memset(&samplerDesc, 0, sizeof(samplerDesc));
    samplerDesc.minFilter = FLUXION_RHI_FILTER_LINEAR;
    samplerDesc.magFilter = FLUXION_RHI_FILTER_LINEAR;
    samplerDesc.mipFilter = FLUXION_RHI_FILTER_NEAREST;
    samplerDesc.addressModeU = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeV = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeW = FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.maxAnisotropy = 1.0f;
    samplerDesc.debugName = "Fluxion.DebugUI.Sampler";
    s_ui.sampler = Fluxion_RHI_CreateSampler(s_ui.device, &samplerDesc);

    FluxionRHIBufferDesc bufferDesc;
    memset(&bufferDesc, 0, sizeof(bufferDesc));
    bufferDesc.size = FLUXION_DEBUG_UI_UNIFORM_BYTES;
    bufferDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_CONSTANT_BUFFER;
    bufferDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    bufferDesc.debugName = "Fluxion.DebugUI.Screen";
    s_ui.uniformBuffer = Fluxion_RHI_CreateBuffer(s_ui.device, &bufferDesc);

    bufferDesc.size = FLUXION_DEBUG_UI_VERTEX_BYTES;
    bufferDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_VERTEX_BUFFER;
    bufferDesc.debugName = "Fluxion.DebugUI.Vertices";
    s_ui.vertexBuffer = Fluxion_RHI_CreateBuffer(s_ui.device, &bufferDesc);

    bufferDesc.size = FLUXION_DEBUG_UI_INDEX_BYTES;
    bufferDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_INDEX_BUFFER;
    bufferDesc.debugName = "Fluxion.DebugUI.Indices";
    s_ui.indexBuffer = Fluxion_RHI_CreateBuffer(s_ui.device, &bufferDesc);

    FluxionRHIBindGroupEntry entries[3];
    memset(entries, 0, sizeof(entries));
    entries[0].binding = 0;
    entries[0].type = FLUXION_RHI_BINDING_TYPE_UNIFORM_BUFFER;
    entries[0].buffer = s_ui.uniformBuffer;
    entries[0].bufferSize = sizeof(f32) * 4u;
    entries[1].binding = 1;
    entries[1].type = FLUXION_RHI_BINDING_TYPE_SAMPLED_TEXTURE;
    entries[1].textureView = s_ui.atlasView;
    entries[2].binding = 2;
    entries[2].type = FLUXION_RHI_BINDING_TYPE_SAMPLER;
    entries[2].sampler = s_ui.sampler;

    FluxionRHIBindGroupDesc groupDesc;
    groupDesc.layout = s_ui.layout;
    groupDesc.entries = entries;
    groupDesc.entryCount = 3;
    s_ui.bindGroup = Fluxion_RHI_CreateBindGroup(s_ui.device, &groupDesc);

    FluxionRHIVertexLayout vertexLayout;
    memset(&vertexLayout, 0, sizeof(vertexLayout));
    vertexLayout.attributes[0].location = 0;
    vertexLayout.attributes[0].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    vertexLayout.attributes[0].offset = 0;
    vertexLayout.attributes[1].location = 1;
    vertexLayout.attributes[1].format = FLUXION_RHI_FORMAT_R32G32_FLOAT;
    vertexLayout.attributes[1].offset = sizeof(f32) * 2u;
    vertexLayout.attributes[2].location = 2;
    vertexLayout.attributes[2].format = FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT;
    vertexLayout.attributes[2].offset = sizeof(f32) * 4u;
    vertexLayout.attributeCount = 3;
    vertexLayout.stride = sizeof(FluxionDebugUIVertex);

    FluxionRHIGraphicsPipelineDesc pipelineDesc;
    memset(&pipelineDesc, 0, sizeof(pipelineDesc));
    pipelineDesc.vertexShader = Fluxion_ShaderProgram_GetVertexShader(s_ui.program);
    pipelineDesc.fragmentShader = Fluxion_ShaderProgram_GetFragmentShader(s_ui.program);
    pipelineDesc.vertexLayout = vertexLayout;
    pipelineDesc.rasterState.cullMode = FLUXION_RHI_CULL_MODE_NONE;
    pipelineDesc.depthState.testEnable = false;
    pipelineDesc.depthState.writeEnable = false;
    pipelineDesc.depthState.compareOp = FLUXION_RHI_COMPARE_OP_ALWAYS;

    // A PANEL COVERS WHAT IS BEHIND IT BY HOW OPAQUE IT IS -- the edges of
    // every glyph are partly transparent, and so is a panel's own
    // background, which is what makes one readable over a bright picture.
    pipelineDesc.blendState.mode = FLUXION_RHI_BLEND_MODE_ALPHA;
    pipelineDesc.topology = FLUXION_RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineDesc.colorFormats[0] = s_ui.colorFormat;
    pipelineDesc.colorFormatCount = 1;
    pipelineDesc.depthFormat = FLUXION_RHI_FORMAT_UNKNOWN;

    const FluxionRHIBindGroupLayoutHandle noLayout = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_GLOBAL] = s_ui.layout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_FRAME] = noLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_MATERIAL] = noLayout;
    pipelineDesc.bindGroupLayouts[FLUXION_RHI_BIND_GROUP_OBJECT] = noLayout;
    pipelineDesc.bindGroupLayoutCount = FLUXION_RHI_BIND_GROUP_GLOBAL + 1;
    pipelineDesc.debugName = "Fluxion.DebugUI.Pipeline";

    s_ui.pipeline = Fluxion_RHI_CreateGraphicsPipeline(s_ui.device, &pipelineDesc);

    return FLUXION_HANDLE_IS_VALID(s_ui.pipeline) && FLUXION_HANDLE_IS_VALID(s_ui.bindGroup) &&
           FLUXION_HANDLE_IS_VALID(s_ui.vertexBuffer) && FLUXION_HANDLE_IS_VALID(s_ui.indexBuffer) &&
           FLUXION_HANDLE_IS_VALID(s_ui.uniformBuffer);
}

// ---------------------------------------------------------------------
// Life.
// ---------------------------------------------------------------------

bool Fluxion_DebugUI_Init(const FluxionDebugUIDesc* desc)
{
    if (desc == NULL || s_ui.ready) return s_ui.ready;

    memset(&s_ui, 0, sizeof(s_ui));

    // ZERO IS NOT "NOTHING": a handle of {0, 0} names the FIRST slot of
    // whatever it points into, and a shutdown that ran after a failure
    // would destroy somebody else's texture with it. Said explicitly,
    // because a struct cleared to zero is otherwise a struct full of
    // valid-looking handles.
    const FluxionRHITextureHandle noTexture = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHITextureViewHandle noView = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    const FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    s_ui.atlasTexture = noTexture;
    s_ui.atlasView = noView;
    s_ui.sampler = (FluxionRHISamplerHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    s_ui.program = (FluxionShaderProgramHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    s_ui.pipeline = (FluxionRHIPipelineHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    s_ui.layout = (FluxionRHIBindGroupLayoutHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    s_ui.bindGroup = (FluxionRHIBindGroupHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    s_ui.uniformBuffer = noBuffer;
    s_ui.vertexBuffer = noBuffer;
    s_ui.indexBuffer = noBuffer;

    s_ui.device = desc->device;
    s_ui.queue = desc->queue;
    s_ui.colorFormat = desc->colorFormat;

    s_ui.contextMemory = Fluxion_Allocator_Alloc(Fluxion_DefaultAllocator(), FLUXION_DEBUG_UI_CONTEXT_BYTES, FLUXION_DEFAULT_ALIGNMENT);
    if (s_ui.contextMemory == NULL) return false;

    if (!nk_init_fixed(&s_ui.context, s_ui.contextMemory, FLUXION_DEBUG_UI_CONTEXT_BYTES, NULL))
    {
        FLUXION_LOG_ERROR(FLUXION_DEBUG_UI_LOG_CATEGORY, "the panel toolkit refused the memory it was given");
        Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), s_ui.contextMemory, FLUXION_DEBUG_UI_CONTEXT_BYTES);
        s_ui.contextMemory = NULL;
        return false;
    }

    struct nk_allocator commandAllocator = FluxionDebugUI_Allocator();
    nk_buffer_init(&s_ui.commands, &commandAllocator, 4096);

    if (!FluxionDebugUI_BakeFont() || !FluxionDebugUI_BuildPipeline())
    {
        Fluxion_DebugUI_Shutdown();
        return false;
    }

    if (s_ui.atlas.default_font != NULL)
    {
        nk_style_set_font(&s_ui.context, &s_ui.atlas.default_font->handle);
    }

    // A TICKED BOX HAS TO LOOK TICKED FROM ACROSS THE ROOM.
    //
    // The toolkit's own theme marks one by filling it a shade DARKER than
    // the box around it, which at this size reads as an empty box -- and
    // a panel whose switches cannot be read at a glance is worse than no
    // panel, because it is confidently wrong. The same brightness goes on
    // a slider's handle, for the same reason.
    const struct nk_color accent = nk_rgb(90, 170, 255);
    const struct nk_color accentBright = nk_rgb(150, 205, 255);

    s_ui.context.style.checkbox.cursor_normal = nk_style_item_color(accent);
    s_ui.context.style.checkbox.cursor_hover = nk_style_item_color(accentBright);
    s_ui.context.style.option.cursor_normal = nk_style_item_color(accent);
    s_ui.context.style.option.cursor_hover = nk_style_item_color(accentBright);
    s_ui.context.style.slider.cursor_normal = nk_style_item_color(accent);
    s_ui.context.style.slider.cursor_hover = nk_style_item_color(accentBright);
    s_ui.context.style.slider.cursor_active = nk_style_item_color(accentBright);

    s_ui.ready = true;
    return true;
}

void Fluxion_DebugUI_Shutdown(void)
{
    if (FLUXION_HANDLE_IS_VALID(s_ui.pipeline)) Fluxion_RHI_DestroyPipeline(s_ui.pipeline);
    if (FLUXION_HANDLE_IS_VALID(s_ui.program)) Fluxion_ShaderProgram_Destroy(s_ui.program);
    if (FLUXION_HANDLE_IS_VALID(s_ui.bindGroup)) Fluxion_RHI_DestroyBindGroup(s_ui.bindGroup);
    if (FLUXION_HANDLE_IS_VALID(s_ui.layout)) Fluxion_RHI_DestroyBindGroupLayout(s_ui.layout);
    if (FLUXION_HANDLE_IS_VALID(s_ui.sampler)) Fluxion_RHI_DestroySampler(s_ui.sampler);
    if (FLUXION_HANDLE_IS_VALID(s_ui.uniformBuffer)) Fluxion_RHI_DestroyBuffer(s_ui.uniformBuffer);
    if (FLUXION_HANDLE_IS_VALID(s_ui.vertexBuffer)) Fluxion_RHI_DestroyBuffer(s_ui.vertexBuffer);
    if (FLUXION_HANDLE_IS_VALID(s_ui.indexBuffer)) Fluxion_RHI_DestroyBuffer(s_ui.indexBuffer);
    if (FLUXION_HANDLE_IS_VALID(s_ui.atlasView)) Fluxion_RHI_DestroyTextureView(s_ui.atlasView);
    if (FLUXION_HANDLE_IS_VALID(s_ui.atlasTexture)) Fluxion_RHI_DestroyTexture(s_ui.atlasTexture);

    if (s_ui.contextMemory != NULL)
    {
        nk_font_atlas_clear(&s_ui.atlas);
        nk_buffer_free(&s_ui.commands);
        nk_free(&s_ui.context);
        Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), s_ui.contextMemory, FLUXION_DEBUG_UI_CONTEXT_BYTES);
    }

    memset(&s_ui, 0, sizeof(s_ui));
}

bool Fluxion_DebugUI_IsReady(void)
{
    return s_ui.ready;
}

// ---------------------------------------------------------------------
// A frame of it.
// ---------------------------------------------------------------------

void Fluxion_DebugUI_BeginFrame(const FluxionDebugUIInput* input, u32 width, u32 height)
{
    if (!s_ui.ready) return;

    s_ui.frameWidth = width;
    s_ui.frameHeight = height;

    // THE TOOLKIT IS TOLD THE STATE, not the changes: it works out what
    // was pressed and released by comparing this frame against the one
    // before, which is exactly the bookkeeping a caller would otherwise
    // have to do to raise events in the first place.
    // WHAT THE FRAME BEFORE THIS ONE LEFT, cleared here rather than after
    // drawing it. A frame that was built and never drawn -- a headless
    // check, a frame where the window was minimised -- would otherwise
    // leave the toolkit believing its panels were still open, and the
    // next one begins the same panel a second time. Clearing where the
    // frame BEGINS makes drawing optional, which is what it should be.
    nk_clear(&s_ui.context);
    nk_buffer_clear(&s_ui.commands);

    nk_input_begin(&s_ui.context);
    if (input != NULL)
    {
        nk_input_motion(&s_ui.context, (int)input->mouseX, (int)input->mouseY);
        nk_input_button(&s_ui.context, NK_BUTTON_LEFT, (int)input->mouseX, (int)input->mouseY, input->mouseDown ? 1 : 0);
        if (input->scroll != 0.0f)
        {
            struct nk_vec2 scroll;
            scroll.x = 0.0f;
            scroll.y = input->scroll;
            nk_input_scroll(&s_ui.context, scroll);
        }
    }
    nk_input_end(&s_ui.context);

    s_ui.inFrame = true;
}

void Fluxion_DebugUI_EndFrame(void)
{
    s_ui.inFrame = false;
}

bool Fluxion_DebugUI_BeginPanel(const char* title, f32 x, f32 y, f32 width, f32 height)
{
    if (!s_ui.ready || !s_ui.inFrame || title == NULL) return false;

    const struct nk_rect bounds = nk_rect(x, y, width, height);
    const nk_flags flags = NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_TITLE | NK_WINDOW_MINIMIZABLE |
                           NK_WINDOW_SCALABLE;

    // The title is also the identity: two panels with the same title are
    // one panel, which is the immediate-mode bargain -- no handle to keep,
    // and no two panels may share a name.
    return nk_begin(&s_ui.context, title, bounds, flags) != 0;
}

void Fluxion_DebugUI_EndPanel(void)
{
    if (!s_ui.ready) return;

    // ALWAYS, EVEN WHEN THE PANEL WAS COLLAPSED. The toolkit pushes state
    // when a panel starts whether or not it draws anything, and a caller
    // that skipped this on the way out would leave that state behind --
    // which is why the API above says to call it in both cases.
    nk_end(&s_ui.context);
}

void Fluxion_DebugUI_Row(f32 height, u32 columns)
{
    if (!s_ui.ready) return;
    nk_layout_row_dynamic(&s_ui.context, height, (int)(columns > 0 ? columns : 1));
}

void Fluxion_DebugUI_Label(const char* text)
{
    if (!s_ui.ready || text == NULL) return;
    nk_label(&s_ui.context, text, NK_TEXT_LEFT);
}

bool Fluxion_DebugUI_Checkbox(const char* label, bool* value)
{
    if (!s_ui.ready || label == NULL || value == NULL) return false;

    int state = *value ? 1 : 0;
    const int changed = nk_checkbox_label(&s_ui.context, label, &state);
    *value = state != 0;
    return changed != 0;
}

bool Fluxion_DebugUI_SliderFloat(const char* label, f32* value, f32 lowest, f32 highest, f32 step)
{
    if (!s_ui.ready || value == NULL) return false;

    if (label != NULL) nk_label(&s_ui.context, label, NK_TEXT_LEFT);

    const f32 before = *value;
    nk_slider_float(&s_ui.context, lowest, value, highest, step > 0.0f ? step : (highest - lowest) / 100.0f);
    return *value != before;
}

bool Fluxion_DebugUI_Button(const char* label)
{
    if (!s_ui.ready || label == NULL) return false;
    return nk_button_label(&s_ui.context, label) != 0;
}

// ---------------------------------------------------------------------
// Putting it on the screen.
// ---------------------------------------------------------------------

void Fluxion_DebugUI_Render(FluxionRHICommandListHandle commandList, FluxionRHITextureViewHandle target, u32 width, u32 height)
{
    if (!s_ui.ready || width == 0 || height == 0) return;
    if (!FLUXION_HANDLE_IS_VALID(target)) return;

    // WHAT SHAPE THE TRIANGLES COME OUT IN. Told rather than assumed: the
    // toolkit will write whatever layout it is given, and this is the one
    // the pipeline above was built for.
    static const struct nk_draw_vertex_layout_element vertexLayout[] = {
        { NK_VERTEX_POSITION, NK_FORMAT_FLOAT, offsetof(FluxionDebugUIVertex, position) },
        { NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, offsetof(FluxionDebugUIVertex, uv) },
        { NK_VERTEX_COLOR, NK_FORMAT_R32G32B32A32_FLOAT, offsetof(FluxionDebugUIVertex, color) },
        { NK_VERTEX_LAYOUT_END }
    };

    struct nk_convert_config config;
    memset(&config, 0, sizeof(config));
    config.vertex_layout = vertexLayout;
    config.vertex_size = sizeof(FluxionDebugUIVertex);
    config.vertex_alignment = _Alignof(FluxionDebugUIVertex);
    config.tex_null = s_ui.nullTexture;
    config.circle_segment_count = 22;
    config.curve_segment_count = 22;
    config.arc_segment_count = 22;
    config.global_alpha = 1.0f;
    config.shape_AA = NK_ANTI_ALIASING_ON;
    config.line_AA = NK_ANTI_ALIASING_ON;

    void* vertexMemory = Fluxion_RHI_MapBuffer(s_ui.vertexBuffer);
    void* indexMemory = Fluxion_RHI_MapBuffer(s_ui.indexBuffer);
    if (vertexMemory == NULL || indexMemory == NULL)
    {
        if (vertexMemory != NULL) Fluxion_RHI_UnmapBuffer(s_ui.vertexBuffer);
        if (indexMemory != NULL) Fluxion_RHI_UnmapBuffer(s_ui.indexBuffer);
        return;
    }

    // STRAIGHT INTO THE BUFFERS THE DEVICE READS. The toolkit writes into
    // whatever memory it is handed, so there is no copy between its
    // output and the draw -- which is the reason the buffers are mapped
    // for the whole of the program's life.
    struct nk_buffer vertices;
    struct nk_buffer indices;
    nk_buffer_init_fixed(&vertices, vertexMemory, FLUXION_DEBUG_UI_VERTEX_BYTES);
    nk_buffer_init_fixed(&indices, indexMemory, FLUXION_DEBUG_UI_INDEX_BYTES);

    const nk_flags converted = nk_convert(&s_ui.context, &s_ui.commands, &vertices, &indices, &config);

    Fluxion_RHI_UnmapBuffer(s_ui.vertexBuffer);
    Fluxion_RHI_UnmapBuffer(s_ui.indexBuffer);

    if ((converted & NK_CONVERT_COMMAND_BUFFER_FULL) != 0 || (converted & NK_CONVERT_VERTEX_BUFFER_FULL) != 0 ||
        (converted & NK_CONVERT_ELEMENT_BUFFER_FULL) != 0)
    {
        // SAID, NOT DRAWN HALF. A panel cut off in the middle looks like a
        // rendering bug and is a size problem.
        FLUXION_LOG_ERROR(FLUXION_DEBUG_UI_LOG_CATEGORY, "the panels came to more geometry than there is room for; this frame has none");
        return;
    }

    f32 screen[4];
    screen[0] = (f32)width;
    screen[1] = (f32)height;
    screen[2] = 0.0f;
    screen[3] = 0.0f;

    void* uniformMemory = Fluxion_RHI_MapBuffer(s_ui.uniformBuffer);
    if (uniformMemory != NULL)
    {
        memcpy(uniformMemory, screen, sizeof(screen));
        Fluxion_RHI_UnmapBuffer(s_ui.uniformBuffer);
    }

    FluxionRHIRenderingAttachment attachment;
    attachment.view = target;

    // KEPT, NOT CLEARED: the panels go ON TOP of the picture that is
    // already there. Clearing here is how a debug panel becomes the only
    // thing on the screen.
    attachment.clear = false;
    attachment.clearColor[0] = 0.0f;
    attachment.clearColor[1] = 0.0f;
    attachment.clearColor[2] = 0.0f;
    attachment.clearColor[3] = 1.0f;

    FluxionRHIRenderingDesc renderingDesc;
    renderingDesc.colorAttachments = &attachment;
    renderingDesc.colorAttachmentCount = 1;
    renderingDesc.depthAttachment = NULL;
    renderingDesc.width = width;
    renderingDesc.height = height;

    Fluxion_RHI_CommandList_BeginRendering(commandList, &renderingDesc);
    Fluxion_RHI_CommandList_SetViewport(commandList, 0.0f, 0.0f, (f32)width, (f32)height, 0.0f, 1.0f);
    Fluxion_RHI_CommandList_SetPipeline(commandList, s_ui.pipeline);
    Fluxion_RHI_CommandList_SetBindGroup(commandList, FLUXION_RHI_BIND_GROUP_GLOBAL, s_ui.bindGroup);
    Fluxion_RHI_CommandList_SetVertexBuffer(commandList, 0, s_ui.vertexBuffer, 0);
    Fluxion_RHI_CommandList_SetIndexBuffer(commandList, s_ui.indexBuffer, 0, true);

    u32 firstIndex = 0;
    const struct nk_draw_command* command = NULL;
    nk_draw_foreach(command, &s_ui.context, &s_ui.commands)
    {
        if (command->elem_count == 0) continue;

        // EACH RUN HAS ITS OWN CLIP RECTANGLE, which is what keeps a
        // panel's contents inside the panel when it is scrolled or
        // resized smaller than what it holds.
        i32 clipX = (i32)command->clip_rect.x;
        i32 clipY = (i32)command->clip_rect.y;
        i32 clipWidth = (i32)command->clip_rect.w;
        i32 clipHeight = (i32)command->clip_rect.h;

        if (clipX < 0) { clipWidth += clipX; clipX = 0; }
        if (clipY < 0) { clipHeight += clipY; clipY = 0; }
        if (clipWidth < 0) clipWidth = 0;
        if (clipHeight < 0) clipHeight = 0;
        if (clipX + clipWidth > (i32)width) clipWidth = (i32)width - clipX;
        if (clipY + clipHeight > (i32)height) clipHeight = (i32)height - clipY;

        if (clipWidth > 0 && clipHeight > 0)
        {
            Fluxion_RHI_CommandList_SetScissor(commandList, clipX, clipY, (u32)clipWidth, (u32)clipHeight);
            Fluxion_RHI_CommandList_DrawIndexed(commandList, (u32)command->elem_count, 1, firstIndex, 0, 0);
        }

        firstIndex += (u32)command->elem_count;
    }

    Fluxion_RHI_CommandList_EndRendering(commandList);
}
