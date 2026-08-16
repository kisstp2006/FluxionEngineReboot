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

#include <Fluxion/RHI/RHI.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraph.h>
#include <Fluxion/RenderCore/RenderGraph/RenderGraphPassRegistry.h>

namespace
{

// Shared by every pass in this file's Execute callbacks: records the
// order Execute was actually called in, so a test can assert on it
// without needing any other introspection hook into the compiled graph.
struct ExecutionLog
{
    int order[16];
    int count;
};

void LogExecute(ExecutionLog* log, int id)
{
    if (log->count < 16) log->order[log->count++] = id;
}

FluxionRHITextureDesc MakeTestTextureDesc(const char* name)
{
    FluxionRHITextureDesc desc;
    desc.width = 64;
    desc.height = 64;
    desc.depth = 1;
    desc.mipLevels = 1;
    desc.arrayLayers = 1;
    desc.sampleCount = 1;
    desc.format = FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;
    desc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_SAMPLED | FLUXION_RHI_TEXTURE_USAGE_RENDER_TARGET;
    desc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    desc.debugName = name;
    return desc;
}

// --- Depth -> Lighting -> Tonemap(imported Backbuffer) chain ---------------

void DepthSetup(FluxionRenderGraphBuilder* builder, void* userData)
{
    (void)userData;
    FluxionRHITextureDesc desc = MakeTestTextureDesc("DepthTex");
    Fluxion_RenderGraphBuilder_CreateTexture(builder, "DepthTex", &desc);
}
void DepthExecute(FluxionRHICommandListHandle commandList, void* userData)
{
    (void)commandList;
    LogExecute((ExecutionLog*)userData, 0);
}

void LightingSetup(FluxionRenderGraphBuilder* builder, void* userData)
{
    (void)userData;
    Fluxion_RenderGraphBuilder_ReadTexture(builder, "DepthTex");
    FluxionRHITextureDesc desc = MakeTestTextureDesc("HDRColor");
    Fluxion_RenderGraphBuilder_CreateTexture(builder, "HDRColor", &desc);
}
void LightingExecute(FluxionRHICommandListHandle commandList, void* userData)
{
    (void)commandList;
    LogExecute((ExecutionLog*)userData, 1);
}

void TonemapSetup(FluxionRenderGraphBuilder* builder, void* userData)
{
    (void)userData;
    Fluxion_RenderGraphBuilder_ReadTexture(builder, "HDRColor");
    Fluxion_RenderGraphBuilder_WriteTexture(builder, "Backbuffer");
}
void TonemapExecute(FluxionRHICommandListHandle commandList, void* userData)
{
    (void)commandList;
    LogExecute((ExecutionLog*)userData, 2);
}

// A pass that creates a resource nothing else ever reads and that is
// never imported -- expected to be culled (never Executed).
void DeadEndSetup(FluxionRenderGraphBuilder* builder, void* userData)
{
    (void)userData;
    FluxionRHITextureDesc desc = MakeTestTextureDesc("DeadTex");
    Fluxion_RenderGraphBuilder_CreateTexture(builder, "DeadTex", &desc);
}
void DeadEndExecute(FluxionRHICommandListHandle commandList, void* userData)
{
    (void)commandList;
    LogExecute((ExecutionLog*)userData, 3);
}

// --- Two passes with a genuine mutual dependency (a real cycle) -----------
//
// Both resources are imported (not created by either pass), which is what
// lets this actually reach the compiler's cycle-detection step instead of
// failing earlier as an "unresolved reference" -- see
// RenderGraphCompiler.cpp's comment on why forward-referencing Read/Write
// calls are allowed during Setup.
void CycleASetup(FluxionRenderGraphBuilder* builder, void* userData)
{
    (void)userData;
    Fluxion_RenderGraphBuilder_ReadTexture(builder, "CycleY");
    Fluxion_RenderGraphBuilder_WriteTexture(builder, "CycleX");
}
void CycleAExecute(FluxionRHICommandListHandle commandList, void* userData) { (void)commandList; (void)userData; }

void CycleBSetup(FluxionRenderGraphBuilder* builder, void* userData)
{
    (void)userData;
    Fluxion_RenderGraphBuilder_ReadTexture(builder, "CycleX");
    Fluxion_RenderGraphBuilder_WriteTexture(builder, "CycleY");
}
void CycleBExecute(FluxionRHICommandListHandle commandList, void* userData) { (void)commandList; (void)userData; }

// --- Two passes that both WRITE the same resource, with no read between
// them -- a legitimate multi-writer pattern (e.g. two overlay passes
// accumulating into the same target) that must compile successfully, NOT
// be mistaken for a cycle (a WRITE is simultaneously a producer and a
// potential dependency source; two writers of the same resource must not
// generate edges in both directions between them). ------------------------

void WriterASetup(FluxionRenderGraphBuilder* builder, void* userData)
{
    (void)userData;
    Fluxion_RenderGraphBuilder_WriteTexture(builder, "SharedTarget");
}
void WriterAExecute(FluxionRHICommandListHandle commandList, void* userData)
{
    (void)commandList;
    LogExecute((ExecutionLog*)userData, 4);
}

void WriterBSetup(FluxionRenderGraphBuilder* builder, void* userData)
{
    (void)userData;
    Fluxion_RenderGraphBuilder_WriteTexture(builder, "SharedTarget");
}
void WriterBExecute(FluxionRHICommandListHandle commandList, void* userData)
{
    (void)commandList;
    LogExecute((ExecutionLog*)userData, 5);
}

struct NullRHIFixture
{
    FluxionRHIInstanceHandle instance;
    FluxionRHIDeviceHandle device;
    FluxionRHICommandListHandle commandList;
};

NullRHIFixture CreateNullRHIFixture()
{
    NullRHIFixture fixture;
    FluxionRHIInstanceDesc instanceDesc = { "RenderCoreTests", false };
    fixture.instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &instanceDesc);

    FluxionRHIAdapterHandle adapters[1];
    Fluxion_RHI_EnumerateAdapters(fixture.instance, adapters, 1);

    FluxionRHIDeviceDesc deviceDesc = { 0 };
    fixture.device = Fluxion_RHI_CreateDevice(adapters[0], &deviceDesc);

    fixture.commandList = Fluxion_RHI_CreateCommandList(fixture.device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    return fixture;
}

void DestroyNullRHIFixture(const NullRHIFixture& fixture)
{
    Fluxion_RHI_DestroyCommandList(fixture.commandList);
    Fluxion_RHI_DestroyDevice(fixture.device);
    Fluxion_RHI_DestroyInstance(fixture.instance);
}

} // namespace

extern "C" void Test_GraphCompile_Run(TestContext* ctx)
{
    Fluxion_RenderGraphPassRegistry_Init();

    FluxionRenderGraphPassType depthType = { "Test_Depth", DepthSetup, DepthExecute };
    FluxionRenderGraphPassType lightingType = { "Test_Lighting", LightingSetup, LightingExecute };
    FluxionRenderGraphPassType tonemapType = { "Test_Tonemap", TonemapSetup, TonemapExecute };
    FluxionRenderGraphPassType deadEndType = { "Test_DeadEnd", DeadEndSetup, DeadEndExecute };
    FluxionRenderGraphPassType cycleAType = { "Test_CycleA", CycleASetup, CycleAExecute };
    FluxionRenderGraphPassType cycleBType = { "Test_CycleB", CycleBSetup, CycleBExecute };
    FluxionRenderGraphPassType writerAType = { "Test_WriterA", WriterASetup, WriterAExecute };
    FluxionRenderGraphPassType writerBType = { "Test_WriterB", WriterBSetup, WriterBExecute };

    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Register(&depthType));
    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Register(&lightingType));
    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Register(&tonemapType));
    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Register(&deadEndType));
    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Register(&cycleAType));
    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Register(&cycleBType));
    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Register(&writerAType));
    TEST_CHECK(ctx, Fluxion_RenderGraphPassRegistry_Register(&writerBType));

    NullRHIFixture fixture = CreateNullRHIFixture();

    FluxionRHITextureDesc backbufferDesc = MakeTestTextureDesc("Backbuffer");
    FluxionRHITextureHandle backbufferTexture = Fluxion_RHI_CreateTexture(fixture.device, &backbufferDesc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(backbufferTexture));

    // --- 1) 3-node chain: order + barrier correctness -----------------------
    {
        ExecutionLog log = { { 0 }, 0 };

        FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(fixture.device);
        Fluxion_RenderGraph_ImportTexture(graph, "Backbuffer", backbufferTexture, FLUXION_RHI_RESOURCE_STATE_PRESENT);
        Fluxion_RenderGraph_AddPassFromRegistry(graph, "Test_Depth", &log);
        Fluxion_RenderGraph_AddPassFromRegistry(graph, "Test_Lighting", &log);
        Fluxion_RenderGraph_AddPassFromRegistry(graph, "Test_Tonemap", &log);

        TEST_CHECK(ctx, Fluxion_RenderGraph_Compile(graph));

        Fluxion_RHI_CommandList_Begin(fixture.commandList);
        Fluxion_RenderGraph_Execute(graph, fixture.commandList);
        Fluxion_RHI_CommandList_End(fixture.commandList);

        TEST_CHECK(ctx, log.count == 3);
        TEST_CHECK(ctx, log.count == 3 && log.order[0] == 0 && log.order[1] == 1 && log.order[2] == 2);

        // A named file rather than a nameless one, so what was written
        // can be looked at afterwards. DumpDot returning true only says
        // it thought it succeeded; a version that wrote nothing at all
        // would say exactly the same thing.
        const char* dotPath = "Test_GraphCompile.dot";
        bool dumped = false;
        long dotSize = 0;

        FILE* dotFile = nullptr;
#if defined(_MSC_VER)
        if (fopen_s(&dotFile, dotPath, "wb") != 0) dotFile = nullptr;
#else
        dotFile = fopen(dotPath, "wb");
#endif
        if (dotFile)
        {
            dumped = Fluxion_RenderGraph_DumpDot(graph, dotFile);
            fclose(dotFile);

#if defined(_MSC_VER)
            if (fopen_s(&dotFile, dotPath, "rb") != 0) dotFile = nullptr;
#else
            dotFile = fopen(dotPath, "rb");
#endif
            if (dotFile)
            {
                fseek(dotFile, 0, SEEK_END);
                dotSize = ftell(dotFile);
                fclose(dotFile);
            }
        }
        TEST_CHECK(ctx, dumped);
        TEST_CHECK(ctx, dotSize > 0);
        remove(dotPath);

        Fluxion_RenderGraph_Destroy(graph);
    }

    // --- 2) Cycle rejection ---------------------------------------------------
    {
        FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(fixture.device);
        Fluxion_RenderGraph_ImportTexture(graph, "CycleX", backbufferTexture, FLUXION_RHI_RESOURCE_STATE_UNDEFINED);
        Fluxion_RenderGraph_ImportTexture(graph, "CycleY", backbufferTexture, FLUXION_RHI_RESOURCE_STATE_UNDEFINED);
        Fluxion_RenderGraph_AddPassFromRegistry(graph, "Test_CycleA", NULL);
        Fluxion_RenderGraph_AddPassFromRegistry(graph, "Test_CycleB", NULL);

        TEST_CHECK(ctx, !Fluxion_RenderGraph_Compile(graph));

        Fluxion_RenderGraph_Destroy(graph);
    }

    // --- 3) Pass culling -------------------------------------------------------
    {
        ExecutionLog log = { { 0 }, 0 };

        FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(fixture.device);
        Fluxion_RenderGraph_ImportTexture(graph, "Backbuffer", backbufferTexture, FLUXION_RHI_RESOURCE_STATE_PRESENT);
        Fluxion_RenderGraph_AddPassFromRegistry(graph, "Test_Depth", &log);
        Fluxion_RenderGraph_AddPassFromRegistry(graph, "Test_Lighting", &log);
        Fluxion_RenderGraph_AddPassFromRegistry(graph, "Test_Tonemap", &log);
        Fluxion_RenderGraph_AddPassFromRegistry(graph, "Test_DeadEnd", &log);

        TEST_CHECK(ctx, Fluxion_RenderGraph_Compile(graph));

        Fluxion_RHI_CommandList_Begin(fixture.commandList);
        Fluxion_RenderGraph_Execute(graph, fixture.commandList);
        Fluxion_RHI_CommandList_End(fixture.commandList);

        // Depth, Lighting, Tonemap ran (id 3, the dead-end pass, must not).
        bool sawDeadEnd = false;
        for (int i = 0; i < log.count; ++i)
        {
            if (log.order[i] == 3) sawDeadEnd = true;
        }
        TEST_CHECK(ctx, log.count == 3);
        TEST_CHECK(ctx, !sawDeadEnd);

        Fluxion_RenderGraph_Destroy(graph);
    }

    // --- 4) Multiple writers of the same resource must not be mistaken
    // for a cycle -----------------------------------------------------------
    {
        ExecutionLog log = { { 0 }, 0 };

        FluxionRenderGraph* graph = Fluxion_RenderGraph_Create(fixture.device);
        Fluxion_RenderGraph_ImportTexture(graph, "SharedTarget", backbufferTexture, FLUXION_RHI_RESOURCE_STATE_UNDEFINED);
        Fluxion_RenderGraph_AddPassFromRegistry(graph, "Test_WriterA", &log);
        Fluxion_RenderGraph_AddPassFromRegistry(graph, "Test_WriterB", &log);

        TEST_CHECK(ctx, Fluxion_RenderGraph_Compile(graph));

        Fluxion_RHI_CommandList_Begin(fixture.commandList);
        Fluxion_RenderGraph_Execute(graph, fixture.commandList);
        Fluxion_RHI_CommandList_End(fixture.commandList);

        TEST_CHECK(ctx, log.count == 2 && log.order[0] == 4 && log.order[1] == 5);

        Fluxion_RenderGraph_Destroy(graph);
    }

    Fluxion_RHI_DestroyTexture(backbufferTexture);
    DestroyNullRHIFixture(fixture);

    Fluxion_RenderGraphPassRegistry_Shutdown();
}
