#include <Fluxion/RenderCore/Renderer/TextureDefaults.h>

#include <Fluxion/Foundation/Log.h>
#include <Fluxion/RenderCore/Renderer/TextureAsset.h>

#include "RendererInternal.h"

#include <string.h>

#define FLUXION_TEXTURE_DEFAULTS_LOG_CATEGORY "TextureDefaults"

// Few enough that a project runs out of imagination before it runs out of
// these, and small enough to search by walking.
#define FLUXION_SAMPLER_CACHE_CAPACITY 32

typedef struct FluxionDefaultTextureEntry
{
    FluxionTextureAsset* asset;
} FluxionDefaultTextureEntry;

static FluxionDefaultTextureEntry s_defaults[FLUXION_DEFAULT_TEXTURE_COUNT];
static bool s_defaultsReady = false;

typedef struct FluxionSamplerCacheEntry
{
    FluxionRHISamplerDesc desc;
    FluxionRHISamplerHandle sampler;
} FluxionSamplerCacheEntry;

static FluxionSamplerCacheEntry s_samplers[FLUXION_SAMPLER_CACHE_CAPACITY];
static u32 s_samplerCount = 0;

// ---------------------------------------------------------------------
// The one-pixel textures.
// ---------------------------------------------------------------------

// Built by writing the cooked form and reading it straight back, rather
// than by making a texture directly.
//
// That is not a roundabout way of doing it: it means these go through the
// same reader and the same upload every other texture goes through. A
// separate path here would be a second way to make a texture, tested only
// by whether the picture looked right.
static FluxionTextureAsset* Fluxion_TextureDefaults_Make(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue,
                                                         FluxionRHIFormat format, const u8 rgba[4])
{
    FluxionTextureAssetData data;
    memset(&data, 0, sizeof(data));
    data.width = 1;
    data.height = 1;
    data.mipCount = 1;
    data.arrayLayers = 1;
    data.format = format;
    data.dimension = FLUXION_RHI_TEXTURE_DIMENSION_2D;
    data.pixels = rgba;
    data.pixelBytes = 4;

    u8 cooked[128];
    FluxionStream writer;
    Fluxion_MemoryStream_InitWriter(&writer, cooked, sizeof(cooked));
    if (!Fluxion_TextureAsset_Write(&writer, &data)) return NULL;

    FluxionTextureAsset* asset = NULL;
    if (!Fluxion_TextureAsset_Read(cooked, Fluxion_Stream_GetPosition(&writer), &asset)) return NULL;

    if (!FluxionRendererInternal_TextureAsset_Upload(asset, device, queue))
    {
        Fluxion_TextureAsset_Destroy(asset);
        return NULL;
    }

    return asset;
}

// The same journey for a cube: one texel a face, six faces.
//
// Written and read back through the ordinary path for the same reason as
// the flat ones -- a separate way of making a texture would be tested
// only by whether the picture looked right, and this one exists precisely
// for the case where there is no picture to look at.
static FluxionTextureAsset* Fluxion_TextureDefaults_MakeCube(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue,
                                                             FluxionRHIFormat format, const u8* facePixels, usize faceBytes)
{
    FluxionTextureAssetData data;
    memset(&data, 0, sizeof(data));
    data.width = 1;
    data.height = 1;
    data.mipCount = 1;
    data.arrayLayers = FLUXION_RHI_CUBE_FACE_COUNT;
    data.format = format;
    data.dimension = FLUXION_RHI_TEXTURE_DIMENSION_CUBE;
    data.pixels = facePixels;
    data.pixelBytes = faceBytes * FLUXION_RHI_CUBE_FACE_COUNT;

    u8 cooked[256];
    FluxionStream writer;
    Fluxion_MemoryStream_InitWriter(&writer, cooked, sizeof(cooked));
    if (!Fluxion_TextureAsset_Write(&writer, &data)) return NULL;

    FluxionTextureAsset* asset = NULL;
    if (!Fluxion_TextureAsset_Read(cooked, Fluxion_Stream_GetPosition(&writer), &asset)) return NULL;

    if (!FluxionRendererInternal_TextureAsset_Upload(asset, device, queue))
    {
        Fluxion_TextureAsset_Destroy(asset);
        return NULL;
    }

    return asset;
}

bool Fluxion_TextureDefaults_Init(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue)
{
    if (s_defaultsReady) return true;

    static const u8 kWhite[4] = { 255, 255, 255, 255 };
    static const u8 kBlack[4] = { 0, 0, 0, 255 };

    // Straight out of the surface, and in a LINEAR format: a normal is a
    // direction, not a colour. Read through an sRGB format it would bend
    // every surface it was bound to, and nothing would report it.
    static const u8 kFlatNormal[4] = { 128, 128, 255, 255 };

    s_defaults[FLUXION_DEFAULT_TEXTURE_WHITE].asset =
        Fluxion_TextureDefaults_Make(device, queue, FLUXION_RHI_FORMAT_R8G8B8A8_SRGB, kWhite);
    s_defaults[FLUXION_DEFAULT_TEXTURE_BLACK].asset =
        Fluxion_TextureDefaults_Make(device, queue, FLUXION_RHI_FORMAT_R8G8B8A8_SRGB, kBlack);
    s_defaults[FLUXION_DEFAULT_TEXTURE_FLAT_NORMAL].asset =
        Fluxion_TextureDefaults_Make(device, queue, FLUXION_RHI_FORMAT_R8G8B8A8_UNORM, kFlatNormal);

    // Half precision, because that is the format an environment is
    // stored in and this stands in for one: a default in a different
    // format would be a texture the shader reads differently from the
    // thing it replaces.
    static const u16 kBlackCube[FLUXION_RHI_CUBE_FACE_COUNT * 4] = { 0 };
    s_defaults[FLUXION_DEFAULT_TEXTURE_BLACK_CUBE].asset =
        Fluxion_TextureDefaults_MakeCube(device, queue, FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT,
                                         (const u8*)kBlackCube, sizeof(u16) * 4);

    for (u32 i = 0; i < FLUXION_DEFAULT_TEXTURE_COUNT; ++i)
    {
        if (s_defaults[i].asset != NULL) continue;

        FLUXION_LOG_ERROR(FLUXION_TEXTURE_DEFAULTS_LOG_CATEGORY, "could not make the built-in textures");
        Fluxion_TextureDefaults_Shutdown();
        return false;
    }

    s_defaultsReady = true;
    return true;
}

void Fluxion_TextureDefaults_Shutdown(void)
{
    for (u32 i = 0; i < FLUXION_DEFAULT_TEXTURE_COUNT; ++i)
    {
        if (s_defaults[i].asset) Fluxion_TextureAsset_Destroy(s_defaults[i].asset);
        s_defaults[i].asset = NULL;
    }
    s_defaultsReady = false;
}

FluxionRHITextureViewHandle Fluxion_TextureDefaults_GetView(FluxionDefaultTexture which)
{
    const FluxionRHITextureViewHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    if (!s_defaultsReady || (u32)which >= (u32)FLUXION_DEFAULT_TEXTURE_COUNT) return invalid;
    if (s_defaults[which].asset == NULL) return invalid;

    return s_defaults[which].asset->view;
}

// ---------------------------------------------------------------------
// The sampler cache.
// ---------------------------------------------------------------------

// Compared field by field rather than with a byte comparison of the
// struct: a struct has padding, and padding is whatever was in the
// caller's stack frame. Two identical descriptions would then miss each
// other in the cache, sometimes, depending on what the caller had done
// beforehand.
static bool Fluxion_SamplerCache_DescEquals(const FluxionRHISamplerDesc* a, const FluxionRHISamplerDesc* b)
{
    return a->minFilter == b->minFilter && a->magFilter == b->magFilter && a->mipFilter == b->mipFilter &&
           a->addressModeU == b->addressModeU && a->addressModeV == b->addressModeV && a->addressModeW == b->addressModeW &&
           a->maxAnisotropy == b->maxAnisotropy;
}

FluxionRHISamplerHandle Fluxion_SamplerCache_Get(FluxionRHIDeviceHandle device, const FluxionRHISamplerDesc* desc)
{
    const FluxionRHISamplerHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (desc == NULL) return invalid;

    for (u32 i = 0; i < s_samplerCount; ++i)
    {
        if (Fluxion_SamplerCache_DescEquals(&s_samplers[i].desc, desc)) return s_samplers[i].sampler;
    }

    if (s_samplerCount >= FLUXION_SAMPLER_CACHE_CAPACITY)
    {
        FLUXION_LOG_ERROR(FLUXION_TEXTURE_DEFAULTS_LOG_CATEGORY, "no room for another distinct sampler");
        return invalid;
    }

    FluxionRHISamplerHandle sampler = Fluxion_RHI_CreateSampler(device, desc);
    if (!FLUXION_HANDLE_IS_VALID(sampler)) return invalid;

    s_samplers[s_samplerCount].desc = *desc;

    // The debug name is not part of what makes two samplers the same, and
    // it points at the caller's memory. Cleared so a later comparison
    // cannot read a string that has gone.
    s_samplers[s_samplerCount].desc.debugName = NULL;
    s_samplers[s_samplerCount].sampler = sampler;
    ++s_samplerCount;

    return sampler;
}

u32 Fluxion_SamplerCache_GetCount(void)
{
    return s_samplerCount;
}

void Fluxion_SamplerCache_Shutdown(void)
{
    for (u32 i = 0; i < s_samplerCount; ++i) Fluxion_RHI_DestroySampler(s_samplers[i].sampler);

    memset(s_samplers, 0, sizeof(s_samplers));
    s_samplerCount = 0;
}
