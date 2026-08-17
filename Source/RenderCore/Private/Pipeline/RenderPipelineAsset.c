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

// The cooked half of a render pipeline asset, its registration with the
// asset system, what its settings do, and which of two of them a view is
// drawn with.

#include <Fluxion/RenderCore/Pipeline/RenderPipelineAsset.h>

#include <Fluxion/Assets/AssetType.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/Memory/Allocator.h>

#include <string.h>

#define FLUXION_RENDER_PIPELINE_ASSET_LOG_CATEGORY "RenderPipelineAsset"

static bool s_registered = false;

// Which pipeline everything that does not name one of its own is drawn
// with. One per program, like the asset database it points into.
static FluxionAssetRef s_projectDefault;

FluxionAssetTypeId Fluxion_RenderPipelineAsset_TypeId(void)
{
    return Fluxion_AssetTypeId_FromName(Fluxion_StringView_FromCStr(FLUXION_RENDER_PIPELINE_ASSET_TYPE_NAME));
}

// ---------------------------------------------------------------------
// What this build can and cannot do.
// ---------------------------------------------------------------------

bool Fluxion_RenderPipelineAsset_AreSettingsSupported(const FluxionRenderPipelineAssetSettings* settings, const char** outUnsupported)
{
    if (settings == NULL) return false;

    const char* unsupported = NULL;

    // In the order the design lists them, so the first complaint about a
    // file that asks for several is the same one every time.
    if (settings->lighting == FLUXION_RENDER_PIPELINE_LIGHTING_CLUSTERED) unsupported = "clustered lighting";
    else if (settings->taa) unsupported = "TAA";
    else if (settings->ssao) unsupported = "SSAO";
    else if (settings->ssr) unsupported = "SSR";
    else if (settings->bloom) unsupported = "bloom";
    else if (settings->msaaSamples > 1) unsupported = "multisampling";

    if (outUnsupported != NULL) *outUnsupported = unsupported;
    return unsupported == NULL;
}

// ---------------------------------------------------------------------
// The format, both directions in one place.
// ---------------------------------------------------------------------

static bool Fluxion_RenderPipelineAsset_SerializeName(FluxionStream* stream, char* name, usize capacity)
{
    u32 length = Fluxion_Stream_IsWriting(stream) ? (u32)strlen(name) : 0;
    Fluxion_Stream_SerializeU32(stream, &length);

    if ((usize)length >= capacity) return false;

    Fluxion_Stream_SerializeBytes(stream, name, length);
    if (Fluxion_Stream_IsReading(stream)) name[length] = '\0';
    return true;
}

static bool Fluxion_RenderPipelineAsset_Serialize(FluxionStream* stream, FluxionRenderPipelineAsset* asset)
{
    u32 magic = FLUXION_RENDER_PIPELINE_ASSET_MAGIC;
    u32 formatVersion = FLUXION_RENDER_PIPELINE_ASSET_FORMAT_VERSION;
    Fluxion_Stream_SerializeU32(stream, &magic);
    Fluxion_Stream_SerializeU32(stream, &formatVersion);

    if (magic != FLUXION_RENDER_PIPELINE_ASSET_MAGIC)
    {
        FLUXION_LOG_ERROR(FLUXION_RENDER_PIPELINE_ASSET_LOG_CATEGORY, "these are not the bytes of a render pipeline");
        return false;
    }
    if (formatVersion > FLUXION_RENDER_PIPELINE_ASSET_FORMAT_VERSION)
    {
        FLUXION_LOG_ERROR(FLUXION_RENDER_PIPELINE_ASSET_LOG_CATEGORY,
                          "this render pipeline was written by a newer build (version %u); refusing to read it", formatVersion);
        return false;
    }

    if (!Fluxion_RenderPipelineAsset_SerializeName(stream, asset->name, sizeof(asset->name))) return false;

    Fluxion_Stream_SerializeBytes(stream, asset->graph.asset.bytes, sizeof(asset->graph.asset.bytes));

    u32 lighting = (u32)asset->settings.lighting;
    u32 shadowQuality = (u32)asset->settings.shadowQuality;
    Fluxion_Stream_SerializeU32(stream, &lighting);
    Fluxion_Stream_SerializeU32(stream, &shadowQuality);
    if (lighting > FLUXION_RENDER_PIPELINE_LIGHTING_CLUSTERED) return false;
    if (shadowQuality > FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_HIGH) return false;
    asset->settings.lighting = (FluxionRenderPipelineLighting)lighting;
    asset->settings.shadowQuality = (FluxionRenderPipelineShadowQuality)shadowQuality;

    u8 taa = asset->settings.taa ? 1u : 0u;
    u8 ssao = asset->settings.ssao ? 1u : 0u;
    u8 ssr = asset->settings.ssr ? 1u : 0u;
    u8 bloom = asset->settings.bloom ? 1u : 0u;
    Fluxion_Stream_SerializeU8(stream, &taa);
    Fluxion_Stream_SerializeU8(stream, &ssao);
    Fluxion_Stream_SerializeU8(stream, &ssr);
    Fluxion_Stream_SerializeU8(stream, &bloom);
    asset->settings.taa = taa != 0;
    asset->settings.ssao = ssao != 0;
    asset->settings.ssr = ssr != 0;
    asset->settings.bloom = bloom != 0;

    Fluxion_Stream_SerializeU32(stream, &asset->settings.msaaSamples);

    if (Fluxion_Stream_HasOverflowed(stream)) return false;

    // Checked on the way in as well as at the cook: a build that dropped
    // support for something, or a file cooked by a build that had it,
    // would otherwise load and quietly render without it.
    const char* unsupported = NULL;
    if (Fluxion_Stream_IsReading(stream) && !Fluxion_RenderPipelineAsset_AreSettingsSupported(&asset->settings, &unsupported))
    {
        FLUXION_LOG_ERROR(FLUXION_RENDER_PIPELINE_ASSET_LOG_CATEGORY,
                          "\"%s\" asks for %s, which this build has no pass for", asset->name, unsupported);
        return false;
    }

    return true;
}

bool Fluxion_RenderPipelineAsset_Write(FluxionStream* stream, const FluxionRenderPipelineAsset* asset)
{
    if (stream == NULL || asset == NULL || !Fluxion_Stream_IsWriting(stream)) return false;

    FluxionRenderPipelineAsset copy = *asset;
    return Fluxion_RenderPipelineAsset_Serialize(stream, &copy);
}

bool Fluxion_RenderPipelineAsset_Read(const u8* bytes, usize size, FluxionRenderPipelineAsset** outAsset)
{
    if (bytes == NULL || outAsset == NULL) return false;

    FluxionAllocator* allocator = Fluxion_DefaultAllocator();
    FluxionRenderPipelineAsset* asset =
        (FluxionRenderPipelineAsset*)Fluxion_Allocator_Alloc(allocator, sizeof(FluxionRenderPipelineAsset), FLUXION_DEFAULT_ALIGNMENT);
    if (asset == NULL) return false;

    memset(asset, 0, sizeof(*asset));

    FluxionStream stream;
    Fluxion_MemoryStream_InitReader(&stream, bytes, size);

    if (!Fluxion_RenderPipelineAsset_Serialize(&stream, asset))
    {
        Fluxion_Allocator_Free(allocator, asset, sizeof(FluxionRenderPipelineAsset));
        return false;
    }

    *outAsset = asset;
    return true;
}

void Fluxion_RenderPipelineAsset_Destroy(FluxionRenderPipelineAsset* asset)
{
    if (asset == NULL) return;
    Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), asset, sizeof(FluxionRenderPipelineAsset));
}

// ---------------------------------------------------------------------
// The asset system's two halves.
// ---------------------------------------------------------------------

static bool Fluxion_RenderPipelineAsset_Load(const u8* bytes, usize size, void** outObject, void* userData)
{
    FLUXION_UNUSED(userData);

    FluxionRenderPipelineAsset* asset = NULL;
    if (!Fluxion_RenderPipelineAsset_Read(bytes, size, &asset)) return false;

    *outObject = asset;
    return true;
}

static void Fluxion_RenderPipelineAsset_Unload(void* object, void* userData)
{
    FLUXION_UNUSED(userData);
    Fluxion_RenderPipelineAsset_Destroy((FluxionRenderPipelineAsset*)object);
}

bool Fluxion_RenderPipelineAsset_RegisterType(void)
{
    if (s_registered) return true;

    FluxionAssetTypeDesc desc;
    memset(&desc, 0, sizeof(desc));

    memcpy(desc.name, FLUXION_RENDER_PIPELINE_ASSET_TYPE_NAME, sizeof(FLUXION_RENDER_PIPELINE_ASSET_TYPE_NAME));
    memcpy(desc.cookedExtension, "fluxpipeline", sizeof("fluxpipeline"));

    // Same as the render graph type beside it: the authored form is read
    // by ParseText, which takes a resolver the import signature has
    // nowhere to put, and a shipped game reads the cooked form alone.
    desc.sourceExtensionCount = 0;
    desc.import = NULL;

    desc.defaultShipPolicy = FLUXION_ASSET_SHIP_COOKED;
    desc.load = Fluxion_RenderPipelineAsset_Load;
    desc.finalize = NULL;
    desc.unload = Fluxion_RenderPipelineAsset_Unload;

    s_registered = Fluxion_AssetTypes_Register(&desc);
    return s_registered;
}

void Fluxion_RenderPipelineAsset_UnregisterType(void)
{
    if (!s_registered) return;

    Fluxion_AssetTypes_Unregister(Fluxion_RenderPipelineAsset_TypeId());
    s_registered = false;
}

// ---------------------------------------------------------------------
// What the settings do.
// ---------------------------------------------------------------------

void Fluxion_RenderPipelineAsset_ApplyToViewDesc(const FluxionRenderPipelineAsset* asset, FluxionRenderViewDesc* desc)
{
    if (asset == NULL || desc == NULL) return;

    // Whole numbers of tiles across, which is what the atlas allocator
    // requires -- four tiles a side at every level, so the shape of the
    // grid stays the same and only its resolution changes.
    switch (asset->settings.shadowQuality)
    {
        case FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_OFF:
            // One tile, and a small one. A graph without a shadow pass
            // never draws into the atlas, and the cleared depth it keeps
            // reads as "the light reached here" -- so this is a lit
            // scene, not a black one, and it costs a quarter of a
            // megabyte to say so.
            desc->shadowAtlasSize = 256;
            desc->shadowTileSize = 256;
            break;
        case FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_LOW:
            desc->shadowAtlasSize = 1024;
            desc->shadowTileSize = 256;
            break;
        case FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_MEDIUM:
            desc->shadowAtlasSize = 2048;
            desc->shadowTileSize = 512;
            break;
        case FLUXION_RENDER_PIPELINE_SHADOW_QUALITY_HIGH:
            desc->shadowAtlasSize = 4096;
            desc->shadowTileSize = 1024;
            break;
    }
}

// ---------------------------------------------------------------------
// Which pipeline a view is drawn with.
// ---------------------------------------------------------------------

void Fluxion_RenderPipelineAsset_SetProjectDefault(FluxionAssetRef pipeline)
{
    s_projectDefault = pipeline;
}

FluxionAssetRef Fluxion_RenderPipelineAsset_GetProjectDefault(void)
{
    return s_projectDefault;
}

FluxionAssetRef Fluxion_RenderPipelineAsset_Resolve(FluxionAssetRef cameraOverride)
{
    return Fluxion_AssetRef_IsSet(cameraOverride) ? cameraOverride : s_projectDefault;
}
