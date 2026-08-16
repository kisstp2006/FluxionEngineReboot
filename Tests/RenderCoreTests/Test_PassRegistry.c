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
