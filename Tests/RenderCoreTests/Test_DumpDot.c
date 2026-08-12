#include "TestFramework.h"

#include <Fluxion/RenderCore/RenderGraph/RenderGraph.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>

#include <stdio.h>
#include <string.h>

static void Test_DumpDot_Setup(FluxionRenderGraphBuilder* builder, void* userData)
{
    (void)userData;
    FluxionRHITextureDesc desc;
    desc.width = 4;
    desc.height = 4;
    desc.depth = 1;
    desc.mipLevels = 1;
    desc.arrayLayers = 1;
    desc.sampleCount = 1;
    desc.format = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    desc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_SAMPLED;
    desc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    desc.debugName = "DumpDotTex";
    Fluxion_RenderGraphBuilder_CreateTexture(builder, "DumpDotTex", &desc);
}

static void Test_DumpDot_Execute(FluxionRHICommandListHandle commandList, void* userData)
{
    (void)commandList;
    (void)userData;
}

static void Test_DumpDot_ConsumerSetup(FluxionRenderGraphBuilder* builder, void* userData)
{
    (void)userData;
    Fluxion_RenderGraphBuilder_ReadTexture(builder, "DumpDotTex");
}

void Test_DumpDot_Run(TestContext* ctx)
{
    Fluxion_RenderGraphPassRegistry_Init();

    FluxionRenderGraphPassType producerType = { "Test_DumpDot_Producer", Test_DumpDot_Setup, Test_DumpDot_Execute };
    FluxionRenderGraphPassType consumerType = { "Test_DumpDot_Consumer", Test_DumpDot_ConsumerSetup, Test_DumpDot_Execute };
    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Register(&producerType));
    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Register(&consumerType));

    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests", false };
    FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &instanceDesc);
    FluxionRHIAdapterHandle adapters[1];
    Fluxion_RHI_EnumerateAdapters(instance, adapters, 1);
    FluxionRHIDeviceDesc deviceDesc = { 0 };
    FluxionRHIDeviceHandle device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);

    FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(device);

    // A graph that hasn't compiled yet: DumpDot must not succeed.
    FILE* tooEarlyFile = tmpfile();
    if (tooEarlyFile)
    {
        TEST_CHECK(ctx, !Fluxion_RenderGraph_DumpDot(graph, tooEarlyFile));
        fclose(tooEarlyFile);
    }

    FluxionRenderGraphPassHandle producerHandle = Fluxion_RenderGraph_AddPassFromRegistry(graph, "Test_DumpDot_Producer", NULL);
    FluxionRenderGraphPassHandle consumerHandle = Fluxion_RenderGraph_AddPassFromRegistry(graph, "Test_DumpDot_Consumer", NULL);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(producerHandle));
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(consumerHandle));

    TEST_CHECK(ctx, Fluxion_RenderGraph_Compile(graph));

    FILE* dotFile = tmpfile();
    if (dotFile)
    {
        TEST_CHECK(ctx, Fluxion_RenderGraph_DumpDot(graph, dotFile));

        rewind(dotFile);
        char buffer[2048];
        usize length = fread(buffer, 1, sizeof(buffer) - 1, dotFile);
        buffer[length] = '\0';
        fclose(dotFile);

        TEST_CHECK(ctx, length > 0);
        // Node names default to "<PassTypeName>#<index>" when a node has
        // no more specific instance name (only JSON-loaded nodes get one).
        TEST_CHECK(ctx, strstr(buffer, "Test_DumpDot_Producer#0") != NULL);
        TEST_CHECK(ctx, strstr(buffer, "Test_DumpDot_Consumer#1") != NULL);
        TEST_CHECK(ctx, strstr(buffer, "digraph") != NULL);
    }
    else
    {
        TEST_CHECK(ctx, false);
    }

    Fluxion_RenderGraph_Destroy(graph);
    Fluxion_RHI_DestroyDevice(device);
    Fluxion_RHI_DestroyInstance(instance);

    Fluxion_RenderGraphPassRegistry_Shutdown();
}
