#include "TestFramework.h"

#include <Fluxion/RenderCore/RenderGraph/RenderGraph.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>

#include <stdio.h>
#include <string.h>

// The .rendergraph JSON format only carries a node's name/type (see
// RenderGraphJsonLoader.c) -- there is no per-node parameter payload to
// route a test-owned pointer through, so this test counts Execute calls
// via a plain static instead of per-node userData.
static int s_jsonPassExecuteCount = 0;

static void Test_JsonLoader_Setup(FluxionRenderGraphBuilder* builder, void* userData)
{
    (void)builder;
    (void)userData;
}

static void Test_JsonLoader_Execute(FluxionRHICommandListHandle commandList, void* userData)
{
    (void)commandList;
    (void)userData;
    ++s_jsonPassExecuteCount;
}

void Test_JsonLoader_Run(TestContext* ctx)
{
    Fluxion_RenderGraphPassRegistry_Init();

    FluxionRenderGraphPassType passType = { "Test_JsonPass", Test_JsonLoader_Setup, Test_JsonLoader_Execute };
    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Register(&passType));

    // --- Valid file: two nodes of a registered type, plus an (unused,
    // informational-only) imports section. ------------------------------------
    static const char validJson[] =
        "{"
        "  \"imports\": [ { \"name\": \"Backbuffer\", \"kind\": \"texture\" } ],"
        "  \"nodes\": ["
        "    { \"name\": \"first\", \"type\": \"Test_JsonPass\" },"
        "    { \"name\": \"second\", \"type\": \"Test_JsonPass\" }"
        "  ]"
        "}";

    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests", false };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &instanceDesc);
    FluxionRHIAdapterHandle adapters[1];
    Fluxion_RHI_EnumerateAdapters(instance, adapters, 1);
    FluxionRHIDeviceDesc deviceDesc = { 0 };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);
    FluxionRHICommandListHandle commandList = Fluxion_RHI_CreateCommandList(device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);

    FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(device);
    TEST_CHECK(ctx, Fluxion_RenderGraph_LoadFromJSON(graph, validJson, strlen(validJson)));
    TEST_CHECK(ctx, Fluxion_RenderGraph_Compile(graph));

    // Node count matches the two entries in the "nodes" array: both by
    // Execute running exactly twice, and by both instance names ("first",
    // "second") the loader assigned showing up in the compiled DOT dump.
    s_jsonPassExecuteCount = 0;
    Fluxion_RHI_CommandList_Begin(commandList);
    Fluxion_RenderGraph_Execute(graph, commandList);
    Fluxion_RHI_CommandList_End(commandList);
    TEST_CHECK(ctx, s_jsonPassExecuteCount == 2);

    FILE* dotFile = tmpfile();
    if (dotFile)
    {
        TEST_CHECK(ctx, Fluxion_RenderGraph_DumpDot(graph, dotFile));
        rewind(dotFile);
        char buffer[2048];
        usize length = fread(buffer, 1, sizeof(buffer) - 1, dotFile);
        buffer[length] = '\0';
        TEST_CHECK(ctx, strstr(buffer, "first") != NULL);
        TEST_CHECK(ctx, strstr(buffer, "second") != NULL);
        fclose(dotFile);
    }
    else
    {
        TEST_CHECK(ctx, false);
    }

    Fluxion_RenderGraph_Destroy(graph);

    // --- Malformed reference: an unregistered pass type name -------------------
    static const char invalidJson[] =
        "{ \"nodes\": [ { \"name\": \"bad\", \"type\": \"Test_JsonPass_DoesNotExist\" } ] }";

    FluxionRenderGraph* invalidGraph = Fluxion_RenderGraph_Create(device);
    TEST_CHECK(ctx, !Fluxion_RenderGraph_LoadFromJSON(invalidGraph, invalidJson, strlen(invalidJson)));
    Fluxion_RenderGraph_Destroy(invalidGraph);

    Fluxion_RHI_DestroyCommandList(commandList);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);

    Fluxion_RenderGraphPassRegistry_Shutdown();
}
