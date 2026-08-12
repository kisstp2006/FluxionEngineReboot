#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>

#include <Fluxion/Foundation/Assert.h>

#include <string.h>

#define FLUXION_RENDER_GRAPH_PASS_INDEX_NOT_FOUND ((usize)-1)

static FluxionRenderGraphPassType s_passTypes[FLUXION_RENDER_GRAPH_MAX_PASS_TYPES];
static usize s_passTypeCount = 0;
static bool s_passRegistryInitialized = false;

void Fluxion_RenderGraphPassRegistry_Init(void)
{
    FLUXION_ASSERT_MSG(!s_passRegistryInitialized, "Fluxion_RenderGraphPassRegistry_Init called twice without a Shutdown in between");
    s_passTypeCount = 0;
    s_passRegistryInitialized = true;
}

void Fluxion_RenderGraphPassRegistry_Shutdown(void)
{
    FLUXION_ASSERT_MSG(s_passRegistryInitialized, "Fluxion_RenderGraphPassRegistry_Shutdown called before Init");
    s_passTypeCount = 0;
    s_passRegistryInitialized = false;
}

static usize Fluxion_FindPassTypeIndex(const char* name)
{
    for (usize i = 0; i < s_passTypeCount; ++i)
    {
        if (strcmp(s_passTypes[i].name, name) == 0)
        {
            return i;
        }
    }
    return FLUXION_RENDER_GRAPH_PASS_INDEX_NOT_FOUND;
}

bool Fluxion_RenderGraphPassRegistry_Register(const FluxionRenderGraphPassType* passType)
{
    FLUXION_ASSERT(s_passRegistryInitialized);
    FLUXION_ASSERT(passType != NULL);
    FLUXION_ASSERT(passType->name != NULL);

    if (s_passTypeCount >= FLUXION_RENDER_GRAPH_MAX_PASS_TYPES) return false;
    if (Fluxion_FindPassTypeIndex(passType->name) != FLUXION_RENDER_GRAPH_PASS_INDEX_NOT_FOUND) return false;

    s_passTypes[s_passTypeCount] = *passType;
    ++s_passTypeCount;
    return true;
}

void Fluxion_RenderGraphPassRegistry_Unregister(const char* name)
{
    FLUXION_ASSERT(s_passRegistryInitialized);

    usize index = Fluxion_FindPassTypeIndex(name);
    if (index == FLUXION_RENDER_GRAPH_PASS_INDEX_NOT_FOUND) return;

    // Swap-with-last removal -- registration order carries no meaning here.
    s_passTypes[index] = s_passTypes[s_passTypeCount - 1];
    --s_passTypeCount;
}

const FluxionRenderGraphPassType* Fluxion_RenderGraphPassRegistry_Find(const char* name)
{
    FLUXION_ASSERT(s_passRegistryInitialized);

    usize index = Fluxion_FindPassTypeIndex(name);
    if (index == FLUXION_RENDER_GRAPH_PASS_INDEX_NOT_FOUND) return NULL;

    return &s_passTypes[index];
}
