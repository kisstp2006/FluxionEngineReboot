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
