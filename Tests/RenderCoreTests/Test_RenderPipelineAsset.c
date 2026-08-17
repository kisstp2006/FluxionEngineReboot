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

#include <Fluxion/RenderCore/Pipeline/RenderPipelineAsset.h>

#include <string.h>

// The one graph this test's database knows about.
static FluxionUUID s_knownGraphId;

static bool Test_RenderPipelineAsset_Resolve(const char* graphName, FluxionUUID* outGraphId, void* context)
{
    int* callCount = (int*)context;
    if (callCount != NULL) ++(*callCount);

    if (strcmp(graphName, "TestGraph") != 0) return false;

    *outGraphId = s_knownGraphId;
    return true;
}

static bool Test_RenderPipelineAsset_Parse(const char* text, FluxionRenderPipelineAsset* outAsset, int* callCount)
{
    return Fluxion_RenderPipelineAsset_ParseText(text, strlen(text), Test_RenderPipelineAsset_Resolve, callCount, outAsset);
}

static void Test_RenderPipelineAsset_TextForm(TestContext* ctx)
{
    FluxionRenderPipelineAsset asset;
    memset(&asset, 0, sizeof(asset));

    static const char fullText[] =
        "{"
        "  \"name\": \"TestPipeline\","
        "  \"graph\": \"TestGraph\","
        "  \"lighting\": \"forward\","
        "  \"shadowQuality\": \"high\","
        "  \"taa\": false,"
        "  \"ssao\": false,"
        "  \"ssr\": false,"
        "  \"bloom\": false,"
        "  \"msaa\": 1"
        "}";

    int resolveCalls = 0;
    TEST_CHECK(ctx, Test_RenderPipelineAsset_Parse(fullText, &asset, &resolveCalls));
    TEST_CHECK(ctx, strcmp(asset.name, "TestPipeline") == 0);
    TEST_CHECK(ctx, Fluxion_UUID_Equals(asset.graph.asset, s_knownGraphId));
    TEST_CHECK(ctx, Fluxion_AssetRef_IsSet(asset.graph));
    TEST_CHECK(ctx, asset.settings.lighting == FLUXION_RENDER_PIPELINE_LIGHTING_FORWARD);
    TEST_CHECK(ctx, asset.settings.shadowQuality == FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_HIGH);
    TEST_CHECK(ctx, resolveCalls == 1);

    // Everything but the graph is optional, and what is left out is the
    // zero value of its field.
    static const char minimalText[] = "{ \"graph\": \"TestGraph\" }";
    memset(&asset, 0xAB, sizeof(asset));
    TEST_CHECK(ctx, Test_RenderPipelineAsset_Parse(minimalText, &asset, NULL));
    TEST_CHECK(ctx, asset.name[0] == '\0');
    TEST_CHECK(ctx, asset.settings.lighting == FLUXION_RENDER_PIPELINE_LIGHTING_FORWARD);
    TEST_CHECK(ctx, asset.settings.shadowQuality == FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_OFF);
    TEST_CHECK(ctx, asset.settings.msaaSamples == 0);

    // A pipeline that names no graph draws nothing.
    static const char noGraphText[] = "{ \"name\": \"Nothing\", \"shadowQuality\": \"low\" }";
    TEST_CHECK(ctx, !Test_RenderPipelineAsset_Parse(noGraphText, &asset, NULL));

    // A graph the database does not have is a broken pipeline, said at
    // the cook rather than at the first frame.
    static const char unknownGraphText[] = "{ \"graph\": \"NoSuchGraph\" }";
    resolveCalls = 0;
    TEST_CHECK(ctx, !Test_RenderPipelineAsset_Parse(unknownGraphText, &asset, &resolveCalls));
    TEST_CHECK(ctx, resolveCalls == 1);

    static const char strangeQualityText[] = "{ \"graph\": \"TestGraph\", \"shadowQuality\": \"cinematic\" }";
    static const char strangeLightingText[] = "{ \"graph\": \"TestGraph\", \"lighting\": \"raytraced\" }";
    static const char notEvenJsonText[] = "{ \"graph\": ";
    TEST_CHECK(ctx, !Test_RenderPipelineAsset_Parse(strangeQualityText, &asset, NULL));
    TEST_CHECK(ctx, !Test_RenderPipelineAsset_Parse(strangeLightingText, &asset, NULL));
    TEST_CHECK(ctx, !Test_RenderPipelineAsset_Parse(notEvenJsonText, &asset, NULL));
}

// The whole of the decision made in this milestone: a setting this build
// has no pass for is refused BY NAME, one at a time, and the file that
// asked for it does not load.
static void Test_RenderPipelineAsset_RefusesWhatItCannotDo(TestContext* ctx)
{
    static const char* const askingTexts[] = {
        "{ \"graph\": \"TestGraph\", \"lighting\": \"clustered\" }",
        "{ \"graph\": \"TestGraph\", \"taa\": true }",
        "{ \"graph\": \"TestGraph\", \"ssao\": true }",
        "{ \"graph\": \"TestGraph\", \"ssr\": true }",
        "{ \"graph\": \"TestGraph\", \"bloom\": true }",
        "{ \"graph\": \"TestGraph\", \"msaa\": 4 }",
    };
    static const char* const expectedNames[] = {
        "clustered lighting", "TAA", "SSAO", "SSR", "bloom", "multisampling",
    };

    FluxionRenderPipelineAsset asset;
    for (usize i = 0; i < sizeof(askingTexts) / sizeof(askingTexts[0]); ++i)
    {
        memset(&asset, 0, sizeof(asset));

        // The resolver must not even be reached: a file is refused for
        // what it asks the renderer to do before a database is consulted
        // about the graph it names.
        int resolveCalls = 0;
        TEST_CHECK(ctx, !Test_RenderPipelineAsset_Parse(askingTexts[i], &asset, &resolveCalls));
        TEST_CHECK(ctx, resolveCalls == 0);
    }

    // And the same answer, with the name in it, straight from the check
    // the parse uses -- which is how a pipeline built in code gets told
    // the same thing.
    FluxionRenderPipelineAssetSettings settings;
    const char* unsupported = NULL;

    memset(&settings, 0, sizeof(settings));
    TEST_CHECK(ctx, Fluxion_RenderPipelineAsset_AreSettingsSupported(&settings, &unsupported));
    TEST_CHECK(ctx, unsupported == NULL);

    for (usize i = 0; i < sizeof(expectedNames) / sizeof(expectedNames[0]); ++i)
    {
        memset(&settings, 0, sizeof(settings));
        switch (i)
        {
            case 0: settings.lighting = FLUXION_RENDER_PIPELINE_LIGHTING_CLUSTERED; break;
            case 1: settings.taa = true; break;
            case 2: settings.ssao = true; break;
            case 3: settings.ssr = true; break;
            case 4: settings.bloom = true; break;
            default: settings.msaaSamples = 4; break;
        }

        unsupported = NULL;
        TEST_CHECK(ctx, !Fluxion_RenderPipelineAsset_AreSettingsSupported(&settings, &unsupported));
        TEST_CHECK(ctx, unsupported != NULL && strcmp(unsupported, expectedNames[i]) == 0);
    }

    // No multisampling has two spellings and both are accepted -- a
    // description that never filled the field in is not asking for
    // anything.
    memset(&settings, 0, sizeof(settings));
    settings.msaaSamples = 1;
    TEST_CHECK(ctx, Fluxion_RenderPipelineAsset_AreSettingsSupported(&settings, NULL));
}

static void Test_RenderPipelineAsset_CookedForm(TestContext* ctx)
{
    FluxionRenderPipelineAsset authored;
    memset(&authored, 0, sizeof(authored));
    static const char text[] = "{ \"name\": \"Cooked\", \"graph\": \"TestGraph\", \"shadowQuality\": \"medium\" }";
    TEST_CHECK(ctx, Test_RenderPipelineAsset_Parse(text, &authored, NULL));

    u8 cooked[512];
    FluxionStream writer;
    Fluxion_MemoryStream_InitWriter(&writer, cooked, sizeof(cooked));
    TEST_CHECK(ctx, Fluxion_RenderPipelineAsset_Write(&writer, &authored));
    TEST_CHECK(ctx, !Fluxion_Stream_HasOverflowed(&writer));
    const usize cookedSize = Fluxion_Stream_GetPosition(&writer);

    FluxionRenderPipelineAsset* readBack = NULL;
    TEST_CHECK(ctx, Fluxion_RenderPipelineAsset_Read(cooked, cookedSize, &readBack));
    if (readBack != NULL)
    {
        TEST_CHECK(ctx, strcmp(readBack->name, "Cooked") == 0);
        TEST_CHECK(ctx, Fluxion_UUID_Equals(readBack->graph.asset, s_knownGraphId));
        TEST_CHECK(ctx, readBack->settings.shadowQuality == FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_MEDIUM);
        Fluxion_RenderPipelineAsset_Destroy(readBack);
    }
    else
    {
        TEST_CHECK(ctx, false);
    }

    FluxionRenderPipelineAsset* refused = NULL;
    static const u8 notAPipeline[16] = { 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9 };
    TEST_CHECK(ctx, !Fluxion_RenderPipelineAsset_Read(notAPipeline, sizeof(notAPipeline), &refused));
    TEST_CHECK(ctx, !Fluxion_RenderPipelineAsset_Read(cooked, cookedSize / 2, &refused));
    TEST_CHECK(ctx, refused == NULL);

    // A cooked file asking for a pass this build does not have is
    // refused on the way IN as well, not only at the cook -- otherwise a
    // file from a build that had it would load here and quietly render
    // without it.
    FluxionRenderPipelineAsset fromElsewhere = authored;
    fromElsewhere.settings.ssao = true;

    Fluxion_MemoryStream_InitWriter(&writer, cooked, sizeof(cooked));
    TEST_CHECK(ctx, Fluxion_RenderPipelineAsset_Write(&writer, &fromElsewhere));
    TEST_CHECK(ctx, !Fluxion_RenderPipelineAsset_Read(cooked, Fluxion_Stream_GetPosition(&writer), &refused));
    TEST_CHECK(ctx, refused == NULL);
}

static void Test_RenderPipelineAsset_ShadowQuality(TestContext* ctx)
{
    static const FluxionRenderPipelineShadowQuality levels[] = {
        FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_OFF,
        FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_LOW,
        FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_MEDIUM,
        FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_HIGH,
    };

    u32 previousTile = 0;
    for (usize i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i)
    {
        FluxionRenderPipelineAsset asset;
        memset(&asset, 0, sizeof(asset));
        asset.settings.shadowQuality = levels[i];

        FluxionRenderViewDesc viewDesc;
        memset(&viewDesc, 0, sizeof(viewDesc));
        Fluxion_RenderPipelineAsset_ApplyToViewDesc(&asset, &viewDesc);

        // Every level has to be an atlas a whole number of tiles across,
        // or the allocator has room in it that nothing can be placed in.
        TEST_CHECK(ctx, viewDesc.shadowTileSize > 0);
        TEST_CHECK(ctx, viewDesc.shadowAtlasSize >= viewDesc.shadowTileSize);
        TEST_CHECK(ctx, (viewDesc.shadowAtlasSize % viewDesc.shadowTileSize) == 0);

        // Sharper at every step up, which is the whole meaning of the
        // setting.
        TEST_CHECK(ctx, viewDesc.shadowTileSize >= previousTile);
        previousTile = viewDesc.shadowTileSize;
    }

    // Off is one tile: nothing draws into it, and a graph with no shadow
    // pass leaves it cleared, which reads as lit.
    {
        FluxionRenderPipelineAsset off;
        memset(&off, 0, sizeof(off));
        off.settings.shadowQuality = FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_OFF;

        FluxionRenderViewDesc viewDesc;
        memset(&viewDesc, 0, sizeof(viewDesc));
        Fluxion_RenderPipelineAsset_ApplyToViewDesc(&off, &viewDesc);
        TEST_CHECK(ctx, viewDesc.shadowAtlasSize == viewDesc.shadowTileSize);
    }
}

static void Test_RenderPipelineAsset_Selection(TestContext* ctx)
{
    FluxionAssetRef nothing;
    memset(&nothing, 0, sizeof(nothing));

    FluxionAssetRef projectDefault;
    projectDefault.asset = Fluxion_UUID_Generate();

    FluxionAssetRef cameraOverride;
    cameraOverride.asset = Fluxion_UUID_Generate();

    // Neither said: nothing to draw with, and the caller can tell that
    // apart from a reference that points at something missing.
    Fluxion_RenderPipelineAsset_SetProjectDefault(nothing);
    TEST_CHECK(ctx, !Fluxion_AssetRef_IsSet(Fluxion_RenderPipelineAsset_Resolve(nothing)));

    // Only the camera said.
    TEST_CHECK(ctx, Fluxion_UUID_Equals(Fluxion_RenderPipelineAsset_Resolve(cameraOverride).asset, cameraOverride.asset));

    // Only the project said.
    Fluxion_RenderPipelineAsset_SetProjectDefault(projectDefault);
    TEST_CHECK(ctx, Fluxion_UUID_Equals(Fluxion_RenderPipelineAsset_GetProjectDefault().asset, projectDefault.asset));
    TEST_CHECK(ctx, Fluxion_UUID_Equals(Fluxion_RenderPipelineAsset_Resolve(nothing).asset, projectDefault.asset));

    // Both said: the camera wins, which is the whole reason a camera may
    // carry one.
    TEST_CHECK(ctx, Fluxion_UUID_Equals(Fluxion_RenderPipelineAsset_Resolve(cameraOverride).asset, cameraOverride.asset));

    // Put back, so nothing that runs after this inherits a default it
    // never set.
    Fluxion_RenderPipelineAsset_SetProjectDefault(nothing);
}

void Test_RenderPipelineAsset_Run(TestContext* ctx)
{
    s_knownGraphId = Fluxion_UUID_Generate();

    Test_RenderPipelineAsset_TextForm(ctx);
    Test_RenderPipelineAsset_RefusesWhatItCannotDo(ctx);
    Test_RenderPipelineAsset_CookedForm(ctx);
    Test_RenderPipelineAsset_ShadowQuality(ctx);
    Test_RenderPipelineAsset_Selection(ctx);
}
