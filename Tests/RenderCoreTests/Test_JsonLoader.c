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
