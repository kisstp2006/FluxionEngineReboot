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

#include <Fluxion/RenderCore/Pipeline/RenderGraphAsset.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>

#include <string.h>

// Two stub pass types, shaped like the two real ones: one fills
// something the other reads, and each writes a resource the frame hands
// in -- which is what keeps both out of the compiler's pass culling,
// since that keeps only what an imported resource depends on.
static int s_fillExecuteCount = 0;
static int s_drawExecuteCount = 0;
static void* s_fillUserData = NULL;
static void* s_drawUserData = NULL;

static void Test_RenderGraphAsset_FillSetup(FluxionRenderGraphBuilder* builder, void* userData)
{
    (void)userData;
    Fluxion_RenderGraphBuilder_WriteBuffer(builder, "Test_Graph.Numbers");
}

static void Test_RenderGraphAsset_FillExecute(FluxionRHICommandListHandle commandList, void* userData)
{
    (void)commandList;
    s_fillUserData = userData;
    ++s_fillExecuteCount;
}

static void Test_RenderGraphAsset_DrawSetup(FluxionRenderGraphBuilder* builder, void* userData)
{
    (void)userData;
    Fluxion_RenderGraphBuilder_ReadBuffer(builder, "Test_Graph.Numbers");
    Fluxion_RenderGraphBuilder_WriteColorTarget(builder, "Test_Graph.Color");
}

static void Test_RenderGraphAsset_DrawExecute(FluxionRHICommandListHandle commandList, void* userData)
{
    (void)commandList;
    s_drawUserData = userData;
    ++s_drawExecuteCount;
}

// The one file those two agree with: it imports what they touch, and
// instantiates one of each.
static const char k_validText[] =
    "{"
    "  \"name\": \"TestGraph\","
    "  \"imports\": ["
    "    { \"name\": \"Test_Graph.Color\", \"kind\": \"texture\" },"
    "    { \"name\": \"Test_Graph.Numbers\", \"kind\": \"buffer\" }"
    "  ],"
    "  \"nodes\": ["
    "    { \"name\": \"fill\", \"type\": \"Test_GraphFillPass\" },"
    "    { \"name\": \"draw\", \"type\": \"Test_GraphDrawPass\" }"
    "  ]"
    "}";

static bool Test_RenderGraphAsset_Parse(const char* text, FluxionRenderGraphAsset* outAsset)
{
    return Fluxion_RenderGraphAsset_ParseText(text, strlen(text), outAsset);
}

static void Test_RenderGraphAsset_TextForm(TestContext* ctx)
{
    FluxionRenderGraphAsset asset;

    memset(&asset, 0, sizeof(asset));
    TEST_CHECK(ctx, Test_RenderGraphAsset_Parse(k_validText, &asset));
    TEST_CHECK(ctx, strcmp(asset.name, "TestGraph") == 0);
    TEST_CHECK(ctx, asset.importCount == 2);
    TEST_CHECK(ctx, asset.imports[0].kind == FLUXION_RENDER_GRAPH_ASSET_IMPORT_TEXTURE);
    TEST_CHECK(ctx, asset.imports[1].kind == FLUXION_RENDER_GRAPH_ASSET_IMPORT_BUFFER);
    TEST_CHECK(ctx, asset.nodeCount == 2);
    TEST_CHECK(ctx, strcmp(asset.nodes[0].name, "fill") == 0);
    TEST_CHECK(ctx, strcmp(asset.nodes[1].passType, "Test_GraphDrawPass") == 0);

    // A pass type nothing registered is NOT the parser's business: an
    // asset gets cooked where no renderer exists, and this parse has to
    // succeed for that to be possible at all.
    static const char unregisteredText[] =
        "{ \"nodes\": [ { \"name\": \"a\", \"type\": \"Test_GraphNobodyRegistered\" } ] }";
    TEST_CHECK(ctx, Test_RenderGraphAsset_Parse(unregisteredText, &asset));

    // An unknown key is skipped, so a file from a later build still
    // reads as far as it makes sense.
    static const char futureKeyText[] =
        "{ \"nodes\": [ { \"name\": \"a\", \"type\": \"Test_GraphFillPass\" } ], \"somethingLater\": { \"x\": [1, 2] } }";
    TEST_CHECK(ctx, Test_RenderGraphAsset_Parse(futureKeyText, &asset));
    TEST_CHECK(ctx, asset.nodeCount == 1);

    // Every way of being malformed, each on its own.
    static const char noNodesText[] = "{ \"name\": \"Empty\" }";
    static const char nodeWithoutTypeText[] = "{ \"nodes\": [ { \"name\": \"a\" } ] }";
    static const char nodeWithoutNameText[] = "{ \"nodes\": [ { \"type\": \"Test_GraphFillPass\" } ] }";
    static const char duplicateNodeText[] =
        "{ \"nodes\": ["
        "  { \"name\": \"a\", \"type\": \"Test_GraphFillPass\" },"
        "  { \"name\": \"a\", \"type\": \"Test_GraphFillPass\" } ] }";
    static const char strangeKindText[] =
        "{ \"imports\": [ { \"name\": \"X\", \"kind\": \"sampler\" } ],"
        "  \"nodes\": [ { \"name\": \"a\", \"type\": \"Test_GraphFillPass\" } ] }";
    static const char importWithoutKindText[] =
        "{ \"imports\": [ { \"name\": \"X\" } ],"
        "  \"nodes\": [ { \"name\": \"a\", \"type\": \"Test_GraphFillPass\" } ] }";
    static const char duplicateImportText[] =
        "{ \"imports\": [ { \"name\": \"X\", \"kind\": \"texture\" }, { \"name\": \"X\", \"kind\": \"texture\" } ],"
        "  \"nodes\": [ { \"name\": \"a\", \"type\": \"Test_GraphFillPass\" } ] }";
    static const char notEvenJsonText[] = "nodes: [";

    TEST_CHECK(ctx, !Test_RenderGraphAsset_Parse(noNodesText, &asset));
    TEST_CHECK(ctx, !Test_RenderGraphAsset_Parse(nodeWithoutTypeText, &asset));
    TEST_CHECK(ctx, !Test_RenderGraphAsset_Parse(nodeWithoutNameText, &asset));
    TEST_CHECK(ctx, !Test_RenderGraphAsset_Parse(duplicateNodeText, &asset));
    TEST_CHECK(ctx, !Test_RenderGraphAsset_Parse(strangeKindText, &asset));
    TEST_CHECK(ctx, !Test_RenderGraphAsset_Parse(importWithoutKindText, &asset));
    TEST_CHECK(ctx, !Test_RenderGraphAsset_Parse(duplicateImportText, &asset));
    TEST_CHECK(ctx, !Test_RenderGraphAsset_Parse(notEvenJsonText, &asset));

    // A file that failed to parse leaves the caller's asset alone rather
    // than half filled in: this one still holds what the last good parse
    // put there.
    TEST_CHECK(ctx, asset.nodeCount == 1);

    // More nodes than a graph can hold, refused here rather than at the
    // point it would fail to build.
    {
        char tooManyNodes[8192];
        int written = snprintf(tooManyNodes, sizeof(tooManyNodes), "{ \"nodes\": [");
        for (u32 i = 0; i <= FLUXION_RENDER_GRAPH_ASSET_MAX_NODES; ++i)
        {
            written += snprintf(tooManyNodes + written, sizeof(tooManyNodes) - (usize)written,
                                "%s{ \"name\": \"n%u\", \"type\": \"Test_GraphFillPass\" }", i == 0 ? "" : ",", i);
        }
        written += snprintf(tooManyNodes + written, sizeof(tooManyNodes) - (usize)written, "] }");

        TEST_CHECK(ctx, written > 0 && (usize)written < sizeof(tooManyNodes));
        TEST_CHECK(ctx, !Test_RenderGraphAsset_Parse(tooManyNodes, &asset));
    }

    // A name longer than the runtime can hold would stop matching what a
    // pass declares, so it is refused rather than shortened.
    {
        char longName[FLUXION_RENDER_GRAPH_ASSET_MAX_NAME_LENGTH + 8];
        memset(longName, 'x', sizeof(longName) - 1);
        longName[sizeof(longName) - 1] = '\0';

        char longNameText[512];
        snprintf(longNameText, sizeof(longNameText), "{ \"nodes\": [ { \"name\": \"%s\", \"type\": \"Test_GraphFillPass\" } ] }", longName);
        TEST_CHECK(ctx, !Test_RenderGraphAsset_Parse(longNameText, &asset));
    }
}

static void Test_RenderGraphAsset_CookedForm(TestContext* ctx)
{
    FluxionRenderGraphAsset authored;
    memset(&authored, 0, sizeof(authored));
    TEST_CHECK(ctx, Test_RenderGraphAsset_Parse(k_validText, &authored));

    u8 cooked[4096];
    FluxionStream writer;
    Fluxion_MemoryStream_InitWriter(&writer, cooked, sizeof(cooked));
    TEST_CHECK(ctx, Fluxion_RenderGraphAsset_Write(&writer, &authored));
    TEST_CHECK(ctx, !Fluxion_Stream_HasOverflowed(&writer));
    const usize cookedSize = Fluxion_Stream_GetPosition(&writer);

    FluxionRenderGraphAsset* readBack = NULL;
    TEST_CHECK(ctx, Fluxion_RenderGraphAsset_Read(cooked, cookedSize, &readBack));
    if (readBack != NULL)
    {
        TEST_CHECK(ctx, strcmp(readBack->name, authored.name) == 0);
        TEST_CHECK(ctx, readBack->importCount == authored.importCount);
        TEST_CHECK(ctx, readBack->nodeCount == authored.nodeCount);
        for (u32 i = 0; i < readBack->importCount && i < authored.importCount; ++i)
        {
            TEST_CHECK(ctx, strcmp(readBack->imports[i].name, authored.imports[i].name) == 0);
            TEST_CHECK(ctx, readBack->imports[i].kind == authored.imports[i].kind);
        }
        for (u32 i = 0; i < readBack->nodeCount && i < authored.nodeCount; ++i)
        {
            TEST_CHECK(ctx, strcmp(readBack->nodes[i].name, authored.nodes[i].name) == 0);
            TEST_CHECK(ctx, strcmp(readBack->nodes[i].passType, authored.nodes[i].passType) == 0);
        }
        Fluxion_RenderGraphAsset_Destroy(readBack);
    }
    else
    {
        TEST_CHECK(ctx, false);
    }

    // Bytes that are not a graph, and bytes that stop halfway. Neither
    // hands anything back to be freed.
    FluxionRenderGraphAsset* refused = NULL;
    static const u8 notAGraph[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    TEST_CHECK(ctx, !Fluxion_RenderGraphAsset_Read(notAGraph, sizeof(notAGraph), &refused));
    TEST_CHECK(ctx, !Fluxion_RenderGraphAsset_Read(cooked, cookedSize / 2, &refused));
    TEST_CHECK(ctx, refused == NULL);
}

// Runs the graph and reports what happened, so each case below reads as
// the one thing it is checking.
static void Test_RenderGraphAsset_RunGraph(FluxionRenderGraph* graph, FluxionRHICommandListHandle commandList)
{
    s_fillExecuteCount = 0;
    s_drawExecuteCount = 0;
    s_fillUserData = NULL;
    s_drawUserData = NULL;

    if (!Fluxion_RenderGraph_Compile(graph)) return;

    Fluxion_RHI_CommandList_Begin(commandList);
    Fluxion_RenderGraph_Execute(graph, commandList);
    Fluxion_RHI_CommandList_End(commandList);
}

static void Test_RenderGraphAsset_Instantiation(TestContext* ctx, FluxionRHIDeviceHandle device, FluxionRHICommandListHandle commandList)
{
    FluxionRenderGraphAsset asset;
    memset(&asset, 0, sizeof(asset));
    TEST_CHECK(ctx, Test_RenderGraphAsset_Parse(k_validText, &asset));

    FluxionRHITextureDesc textureDesc;
    memset(&textureDesc, 0, sizeof(textureDesc));
    textureDesc.width = 16;
    textureDesc.height = 16;
    textureDesc.depth = 1;
    textureDesc.mipLevels = 1;
    textureDesc.arrayLayers = 1;
    textureDesc.sampleCount = 1;
    textureDesc.format = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET | FLUXION_RHI_TEXTURE_USAGE_SAMPLED;
    textureDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    textureDesc.debugName = "Test_RenderGraphAsset.Color";
    const FluxionRHITextureHandle texture = Fluxion_RHI_CreateTexture(device, &textureDesc);

    FluxionRHIBufferDesc bufferDesc;
    memset(&bufferDesc, 0, sizeof(bufferDesc));
    bufferDesc.size = 256;
    bufferDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_STORAGE_BUFFER;
    bufferDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    bufferDesc.debugName = "Test_RenderGraphAsset.Numbers";
    const FluxionRHIBufferHandle buffer = Fluxion_RHI_CreateBuffer(device, &bufferDesc);

    FluxionRenderGraphBinding bindings[2];
    memset(bindings, 0, sizeof(bindings));
    bindings[0].name = "Test_Graph.Color";
    bindings[0].texture = texture;
    bindings[0].currentState = FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
    bindings[1].name = "Test_Graph.Numbers";
    bindings[1].buffer = buffer;
    bindings[1].currentState = FLUXION_RHI_RESOURCE_STATE_SHADER_READ;

    FluxionRenderGraphInstantiateDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.bindings = bindings;
    desc.bindingCount = 2;

    // --- Everything bound: it builds, compiles and runs ------------------
    {
        FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(device);
        TEST_CHECK(ctx, Fluxion_RenderGraphAsset_Instantiate(&asset, graph, &desc));

        Test_RenderGraphAsset_RunGraph(graph, commandList);

        // Both nodes ran, which is only possible because the bindings put
        // real handles behind the names the file declared: a pass whose
        // resources went unresolved fails Compile instead.
        TEST_CHECK(ctx, s_fillExecuteCount == 1);
        TEST_CHECK(ctx, s_drawExecuteCount == 1);

        // No resolver was given, so every node got the context itself --
        // which was NULL here, and that is what they should have seen.
        TEST_CHECK(ctx, s_fillUserData == NULL && s_drawUserData == NULL);

        // The instance names from the file made it into the graph, which
        // is what a dependency dump and a diagnostic point at.
        FILE* dot = tmpfile();
        if (dot != NULL)
        {
            TEST_CHECK(ctx, Fluxion_RenderGraph_DumpDot(graph, dot));
            rewind(dot);
            char text[4096];
            const usize length = fread(text, 1, sizeof(text) - 1, dot);
            text[length] = '\0';
            TEST_CHECK(ctx, strstr(text, "fill") != NULL);
            TEST_CHECK(ctx, strstr(text, "draw") != NULL);
            fclose(dot);
        }
        else
        {
            TEST_CHECK(ctx, false);
        }

        Fluxion_RenderGraph_Destroy(graph);
    }

    // --- A declared import with nothing bound to it ----------------------
    {
        FluxionRenderGraphInstantiateDesc missing = desc;
        missing.bindingCount = 1; // the buffer is left unbound

        FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(device);
        TEST_CHECK(ctx, !Fluxion_RenderGraphAsset_Instantiate(&asset, graph, &missing));

        // And nothing was added on the way to saying so -- a refused
        // instantiation leaves a graph to throw away, not half a frame.
        Test_RenderGraphAsset_RunGraph(graph, commandList);
        TEST_CHECK(ctx, s_fillExecuteCount == 0 && s_drawExecuteCount == 0);

        Fluxion_RenderGraph_Destroy(graph);
    }

    // --- A binding for a name the file does not declare ------------------
    {
        FluxionRenderGraphBinding misspelt[3];
        memcpy(misspelt, bindings, sizeof(bindings));
        misspelt[2] = bindings[0];
        misspelt[2].name = "Test_Graph.Colour";

        FluxionRenderGraphInstantiateDesc typo = desc;
        typo.bindings = misspelt;
        typo.bindingCount = 3;

        FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(device);
        TEST_CHECK(ctx, !Fluxion_RenderGraphAsset_Instantiate(&asset, graph, &typo));
        Fluxion_RenderGraph_Destroy(graph);
    }

    // --- A binding holding the wrong kind of handle ----------------------
    {
        FluxionRenderGraphBinding wrongKind[2];
        memcpy(wrongKind, bindings, sizeof(bindings));
        wrongKind[1].buffer = (FluxionRHIBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
        wrongKind[1].texture = texture;

        FluxionRenderGraphInstantiateDesc mixed = desc;
        mixed.bindings = wrongKind;

        FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(device);
        TEST_CHECK(ctx, !Fluxion_RenderGraphAsset_Instantiate(&asset, graph, &mixed));
        Fluxion_RenderGraph_Destroy(graph);
    }

    // --- A node whose pass type nothing registered -----------------------
    {
        FluxionRenderGraphAsset unregistered = asset;
        memcpy(unregistered.nodes[1].passType, "Test_GraphNobodyRegistered", sizeof("Test_GraphNobodyRegistered"));

        FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(device);
        TEST_CHECK(ctx, !Fluxion_RenderGraphAsset_Instantiate(&unregistered, graph, &desc));

        // Not even the node before it, which was perfectly good.
        Test_RenderGraphAsset_RunGraph(graph, commandList);
        TEST_CHECK(ctx, s_fillExecuteCount == 0 && s_drawExecuteCount == 0);

        Fluxion_RenderGraph_Destroy(graph);
    }

    Fluxion_RHI_DestroyBuffer(buffer);
    Fluxion_RHI_DestroyTexture(texture);
}

// One pointer per pass type, which is the whole point of asking per node
// rather than handing one pointer to all of them.
static int s_fillContext = 0;
static int s_drawContext = 0;

static void* Test_RenderGraphAsset_ResolveUserData(const char* passTypeName, const char* nodeName, void* context)
{
    (void)nodeName;
    TestContext* ctx = (TestContext*)context;
    TEST_CHECK(ctx, ctx != NULL);

    if (strcmp(passTypeName, "Test_GraphFillPass") == 0) return &s_fillContext;
    return &s_drawContext;
}

static void Test_RenderGraphAsset_PerNodeUserData(TestContext* ctx, FluxionRHIDeviceHandle device, FluxionRHICommandListHandle commandList,
                                                  FluxionRHITextureHandle texture, FluxionRHIBufferHandle buffer)
{
    FluxionRenderGraphAsset asset;
    memset(&asset, 0, sizeof(asset));
    TEST_CHECK(ctx, Test_RenderGraphAsset_Parse(k_validText, &asset));

    FluxionRenderGraphBinding bindings[2];
    memset(bindings, 0, sizeof(bindings));
    bindings[0].name = "Test_Graph.Color";
    bindings[0].texture = texture;
    bindings[0].currentState = FLUXION_RHI_RESOURCE_STATE_UNDEFINED;
    bindings[1].name = "Test_Graph.Numbers";
    bindings[1].buffer = buffer;
    bindings[1].currentState = FLUXION_RHI_RESOURCE_STATE_SHADER_READ;

    FluxionRenderGraphInstantiateDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.bindings = bindings;
    desc.bindingCount = 2;
    desc.resolveUserData = Test_RenderGraphAsset_ResolveUserData;
    desc.context = ctx;

    FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(device);
    TEST_CHECK(ctx, Fluxion_RenderGraphAsset_Instantiate(&asset, graph, &desc));

    Test_RenderGraphAsset_RunGraph(graph, commandList);
    TEST_CHECK(ctx, s_fillUserData == &s_fillContext);
    TEST_CHECK(ctx, s_drawUserData == &s_drawContext);

    Fluxion_RenderGraph_Destroy(graph);
}

void Test_RenderGraphAsset_Run(TestContext* ctx)
{
    Fluxion_RenderGraphPassRegistry_Init();

    const FluxionRenderGraphPassType fillType = { "Test_GraphFillPass", Test_RenderGraphAsset_FillSetup, Test_RenderGraphAsset_FillExecute };
    const FluxionRenderGraphPassType drawType = { "Test_GraphDrawPass", Test_RenderGraphAsset_DrawSetup, Test_RenderGraphAsset_DrawExecute };
    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Register(&fillType));
    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Register(&drawType));

    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests", false };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &instanceDesc);
    FluxionRHIAdapterHandle adapters[1];
    Fluxion_RHI_EnumerateAdapters(instance, adapters, 1);
    FluxionRHIDeviceDesc deviceDesc = { 0 };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    FluxionRHICommandListHandle commandList = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);

    Test_RenderGraphAsset_TextForm(ctx);
    Test_RenderGraphAsset_CookedForm(ctx);
    Test_RenderGraphAsset_Instantiation(ctx, device, commandList);

    {
        FluxionRHITextureDesc textureDesc;
        memset(&textureDesc, 0, sizeof(textureDesc));
        textureDesc.width = 16;
        textureDesc.height = 16;
        textureDesc.depth = 1;
        textureDesc.mipLevels = 1;
        textureDesc.arrayLayers = 1;
        textureDesc.sampleCount = 1;
        textureDesc.format = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET | FLUXION_RHI_TEXTURE_USAGE_SAMPLED;
        textureDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
        textureDesc.debugName = "Test_RenderGraphAsset.Color";
        const FluxionRHITextureHandle texture = Fluxion_RHI_CreateTexture(device, &textureDesc);

        FluxionRHIBufferDesc bufferDesc;
        memset(&bufferDesc, 0, sizeof(bufferDesc));
        bufferDesc.size = 256;
        bufferDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_STORAGE_BUFFER;
        bufferDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
        bufferDesc.debugName = "Test_RenderGraphAsset.Numbers";
        const FluxionRHIBufferHandle buffer = Fluxion_RHI_CreateBuffer(device, &bufferDesc);

        Test_RenderGraphAsset_PerNodeUserData(ctx, device, commandList, texture, buffer);

        Fluxion_RHI_DestroyBuffer(buffer);
        Fluxion_RHI_DestroyTexture(texture);
    }

    Fluxion_RHI_DestroyCommandList(commandList);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);

    Fluxion_RenderGraphPassRegistry_Shutdown();
}
