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
