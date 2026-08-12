#include "TestFramework.h"

#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>

#include <stdio.h>

static void Test_PassRegistry_SetupNoop(FluxionRenderGraphBuilder* builder, void* userData) { (void)builder; (void)userData; }
static void Test_PassRegistry_ExecuteNoop(FluxionRHICommandListHandle commandList, void* userData) { (void)commandList; (void)userData; }

void Test_PassRegistry_Run(TestContext* ctx)
{
    Fluxion_RenderGraphPassRegistry_Init();

    FluxionRenderGraphPassType typeA = { "TestPassA", Test_PassRegistry_SetupNoop, Test_PassRegistry_ExecuteNoop };
    FluxionRenderGraphPassType typeB = { "TestPassB", Test_PassRegistry_SetupNoop, Test_PassRegistry_ExecuteNoop };

    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Register(&typeA));
    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Register(&typeB));

    const FluxionRenderGraphPassType* foundA = Fluxion_RenderGraphPassRegistry_Find("TestPassA");
    const FluxionRenderGraphPassType* foundB = Fluxion_RenderGraphPassRegistry_Find("TestPassB");
    TEST_CHECK(ctx, foundA != NULL && foundA->setup == Test_PassRegistry_SetupNoop);
    TEST_CHECK(ctx, foundB != NULL && foundB->execute == Test_PassRegistry_ExecuteNoop);

    // Duplicate name registration is rejected.
    FluxionRenderGraphPassType duplicateA = { "TestPassA", Test_PassRegistry_SetupNoop, Test_PassRegistry_ExecuteNoop };
    TEST_CHECK(ctx, !Fluxion_RenderGraphPassRegistry_Register(&duplicateA));

    // Unknown name lookup fails.
    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Find("NoSuchPass") == NULL);

    // Unregister then Find returns NULL.
    Fluxion_RenderGraphPassRegistry_Unregister("TestPassA");
    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Find("TestPassA") == NULL);
    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Find("TestPassB") != NULL); // untouched

    // Registry-full rejected past FLUXION_RENDER_GRAPH_MAX_PASS_TYPES.
    // TestPassB already occupies one slot; fill the rest.
    char names[FLUXION_RENDER_GRAPH_MAX_PASS_TYPES][32];
    FluxionRenderGraphPassType fillerTypes[FLUXION_RENDER_GRAPH_MAX_PASS_TYPES];
    int registeredCount = 0;
    for (int i = 0; i < FLUXION_RENDER_GRAPH_MAX_PASS_TYPES; ++i)
    {
        snprintf(names[i], sizeof(names[i]), "FillerPass%d", i);
        fillerTypes[i].name = names[i];
        fillerTypes[i].setup = Test_PassRegistry_SetupNoop;
        fillerTypes[i].execute = Test_PassRegistry_ExecuteNoop;
        if (Fluxion_RenderGraphPassRegistry_Register(&fillerTypes[i]))
        {
            ++registeredCount;
        }
    }

    // One slot was already used by TestPassB, so exactly MAX-1 filler
    // registrations should have succeeded before the registry filled up.
    TEST_CHECK(ctx, registeredCount == FLUXION_RENDER_GRAPH_MAX_PASS_TYPES - 1);

    FluxionRenderGraphPassType overflow = { "OneTooMany", Test_PassRegistry_SetupNoop, Test_PassRegistry_ExecuteNoop };
    TEST_CHECK(ctx, !Fluxion_RenderGraphPassRegistry_Register(&overflow));

    Fluxion_RenderGraphPassRegistry_Shutdown();
}
