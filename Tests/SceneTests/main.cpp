#include "TestFramework.h"

#include <Fluxion/Core/Reflection/Registry.h>

#include <cstdio>

void Test_Hierarchy_Run(TestContext& ctx);
void Test_Transform_Run(TestContext& ctx);
void Test_Components_Run(TestContext& ctx);
void Test_Attributes_Run(TestContext& ctx);
void Test_EngineApi_Run(TestContext& ctx);
void Test_Reload_Run(TestContext& ctx);
void Test_DataComponents_Run(TestContext& ctx);
void Test_Archetype_Run(TestContext& ctx);
void Test_TransformUpdate_Run(TestContext& ctx);
void Test_Systems_Run(TestContext& ctx);
void Test_ScriptReflection_Run(TestContext& ctx);
void Test_SceneSerialization_Run(TestContext& ctx);
void Test_EntityUUID_Run(TestContext& ctx);
void Test_CommandBuffer_Run(TestContext& ctx);
void Test_World_Run(TestContext& ctx);
void Test_SceneAssetReferences_Run(TestContext& ctx);
void Test_SceneLights_Run(TestContext& ctx);

int main()
{
    TestContext ctx;

    std::fprintf(stderr, "Running SceneTests...\n");

    // Brought up once for the whole run rather than case by case: every
    // object a scene makes carries a transform, and the storage takes that
    // component's size from here. So this is not something an individual
    // test opts into -- it is what a scene needs in order to exist.
    Fluxion_Reflection_Init();

    Test_Hierarchy_Run(ctx);
    Test_Transform_Run(ctx);
    Test_Components_Run(ctx);
    Test_Attributes_Run(ctx);
    Test_EngineApi_Run(ctx);
    Test_Reload_Run(ctx);
    Test_DataComponents_Run(ctx);
    Test_Archetype_Run(ctx);
    Test_TransformUpdate_Run(ctx);
    Test_Systems_Run(ctx);
    Test_ScriptReflection_Run(ctx);
    Test_SceneSerialization_Run(ctx);
    Test_EntityUUID_Run(ctx);
    Test_CommandBuffer_Run(ctx);
    Test_World_Run(ctx);
    Test_SceneAssetReferences_Run(ctx);
    Test_SceneLights_Run(ctx);

    Fluxion_Reflection_Shutdown();

    if (ctx.failures == 0)
    {
        std::fprintf(stderr, "All SceneTests passed.\n");
        return 0;
    }

    std::fprintf(stderr, "%d SceneTests check(s) failed.\n", ctx.failures);
    return 1;
}
