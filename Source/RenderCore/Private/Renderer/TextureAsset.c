#include <Fluxion/RenderCore/Renderer/TextureAsset.h>

#include "RendererInternal.h"

#include <Fluxion/Assets/AssetType.h>
#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/Memory/Allocator.h>

#include <string.h>

#define FLUXION_TEXTURE_ASSET_LOG_CATEGORY "TextureAsset"

typedef struct FluxionTextureAssetContext
{
    FluxionRHIDeviceHandle device;
    FluxionRHIQueueHandle queue;
} FluxionTextureAssetContext;

static FluxionTextureAssetContext s_context;
static bool s_registered = false;

// The asset and its pixels in one allocation, for the same reason the
// mesh does it: three separate ones would have to be freed in three
// places, all of which can be forgotten one at a time.
typedef struct FluxionTextureAssetBlock
{
    FluxionTextureAsset asset;
    usize totalSize;
} FluxionTextureAssetBlock;

FluxionAssetTypeId Fluxion_TextureAsset_TypeId(void)
{
    return Fluxion_AssetTypeId_FromName(Fluxion_StringView_FromCStr(FLUXION_TEXTURE_ASSET_TYPE_NAME));
}

// ---------------------------------------------------------------------
// Sizes.
// ---------------------------------------------------------------------

static u32 Fluxion_TextureAsset_BytesPerPixel(FluxionRHIFormat format)
{
    switch (format)
    {
        case FLUXION_RHI_FORMAT_R8G8B8A8_UNORM:
        case FLUXION_RHI_FORMAT_R8G8B8A8_SRGB:
        case FLUXION_RHI_FORMAT_B8G8R8A8_UNORM:
        case FLUXION_RHI_FORMAT_B8G8R8A8_SRGB:
        case FLUXION_RHI_FORMAT_R32_FLOAT:
            return 4;

        case FLUXION_RHI_FORMAT_R32G32_FLOAT:
        case FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT:
            return 8;

        case FLUXION_RHI_FORMAT_R32G32B32_FLOAT:
            return 12;

        case FLUXION_RHI_FORMAT_R32G32B32A32_FLOAT:
            return 16;

        // Everything else, including the depth formats and -- for now --
        // every compressed one. Zero rather than a guess: a size worked
        // out from a format nothing here understands would be a
        // confidently wrong number, and an allocation made from it would
        // be silently too small.
        default:
            return 0;
    }
}

u32 Fluxion_TextureAsset_GetLevelExtent(u32 base, u32 level)
{
    u32 extent = base >> level;
    return extent > 0 ? extent : 1u;
}

usize Fluxion_TextureAsset_GetLevelByteSize(FluxionRHIFormat format, u32 width, u32 height)
{
    const u32 bytesPerPixel = Fluxion_TextureAsset_BytesPerPixel(format);
    if (bytesPerPixel == 0 || width == 0 || height == 0) return 0;

    return (usize)width * (usize)height * (usize)bytesPerPixel;
}

usize Fluxion_TextureAsset_GetTotalByteSize(FluxionRHIFormat format, u32 width, u32 height, u32 mipCount, u32 arrayLayers)
{
    if (mipCount == 0 || arrayLayers == 0) return 0;
    if (Fluxion_TextureAsset_BytesPerPixel(format) == 0) return 0;

    usize total = 0;
    for (u32 level = 0; level < mipCount; ++level)
    {
        const usize levelSize = Fluxion_TextureAsset_GetLevelByteSize(format,
                                                                     Fluxion_TextureAsset_GetLevelExtent(width, level),
                                                                     Fluxion_TextureAsset_GetLevelExtent(height, level));
        if (levelSize == 0) return 0;
        total += levelSize;
    }

    return total * (usize)arrayLayers;
}

// ---------------------------------------------------------------------
// The format, both directions in one place.
// ---------------------------------------------------------------------

static bool Fluxion_TextureAsset_DescribesSomethingReal(u32 width, u32 height, u32 mipCount, u32 arrayLayers, FluxionRHIFormat format)
{
    if (width == 0 || height == 0 || arrayLayers == 0) return false;
    if (mipCount == 0 || mipCount > FLUXION_TEXTURE_ASSET_MAX_MIPS) return false;
    if (Fluxion_TextureAsset_BytesPerPixel(format) == 0) return false;

    // More levels than halving the larger side can produce. A file
    // claiming them describes something that cannot exist, and reading it
    // would mean reading past whatever it does hold.
    u32 largest = width > height ? width : height;
    u32 possible = 1;
    while (largest > 1) { largest >>= 1; ++possible; }

    return mipCount <= possible;
}

bool Fluxion_TextureAsset_Write(FluxionStream* stream, const FluxionTextureAssetData* data)
{
    if (!stream || !data || !Fluxion_Stream_IsWriting(stream)) return false;
    if (!Fluxion_TextureAsset_DescribesSomethingReal(data->width, data->height, data->mipCount, data->arrayLayers, data->format)) return false;
    if (data->pixels == NULL) return false;

    const usize expected = Fluxion_TextureAsset_GetTotalByteSize(data->format, data->width, data->height, data->mipCount, data->arrayLayers);
    if (expected == 0 || data->pixelBytes != expected)
    {
        FLUXION_LOG_ERROR(FLUXION_TEXTURE_ASSET_LOG_CATEGORY,
                          "a texture's pixels are %zu bytes where its own size and format say %zu", data->pixelBytes, expected);
        return false;
    }

    u32 magic = FLUXION_TEXTURE_ASSET_MAGIC;
    u32 formatVersion = FLUXION_TEXTURE_ASSET_FORMAT_VERSION;
    Fluxion_Stream_SerializeU32(stream, &magic);
    Fluxion_Stream_SerializeU32(stream, &formatVersion);

    u32 width = data->width;
    u32 height = data->height;
    u32 mipCount = data->mipCount;
    u32 arrayLayers = data->arrayLayers;
    u32 format = (u32)data->format;
    Fluxion_Stream_SerializeU32(stream, &width);
    Fluxion_Stream_SerializeU32(stream, &height);
    Fluxion_Stream_SerializeU32(stream, &mipCount);
    Fluxion_Stream_SerializeU32(stream, &arrayLayers);
    Fluxion_Stream_SerializeU32(stream, &format);

    // The byte count travels with the pixels rather than being worked out
    // again on the way in. A reader that recomputed it would agree with
    // this writer only for as long as both computed it the same way, and
    // a compressed format arriving later is exactly when they would stop.
    u32 pixelBytes = (u32)data->pixelBytes;
    Fluxion_Stream_SerializeU32(stream, &pixelBytes);
    Fluxion_Stream_SerializeBytes(stream, (void*)data->pixels, pixelBytes);

    return !Fluxion_Stream_HasOverflowed(stream);
}

bool Fluxion_TextureAsset_Read(const u8* bytes, usize size, FluxionTextureAsset** outAsset)
{
    FluxionStream stream;
    Fluxion_MemoryStream_InitReader(&stream, bytes, size);

    u32 magic = 0;
    u32 formatVersion = 0;
    Fluxion_Stream_SerializeU32(&stream, &magic);
    Fluxion_Stream_SerializeU32(&stream, &formatVersion);

    if (magic != FLUXION_TEXTURE_ASSET_MAGIC)
    {
        FLUXION_LOG_ERROR(FLUXION_TEXTURE_ASSET_LOG_CATEGORY, "these are not the bytes of a texture");
        return false;
    }

    if (formatVersion > FLUXION_TEXTURE_ASSET_FORMAT_VERSION)
    {
        FLUXION_LOG_ERROR(FLUXION_TEXTURE_ASSET_LOG_CATEGORY,
                          "texture was written by a newer build (version %u); refusing to read it", formatVersion);
        return false;
    }

    u32 width = 0;
    u32 height = 0;
    u32 mipCount = 0;
    u32 arrayLayers = 0;
    u32 format = 0;
    u32 pixelBytes = 0;
    Fluxion_Stream_SerializeU32(&stream, &width);
    Fluxion_Stream_SerializeU32(&stream, &height);
    Fluxion_Stream_SerializeU32(&stream, &mipCount);
    Fluxion_Stream_SerializeU32(&stream, &arrayLayers);
    Fluxion_Stream_SerializeU32(&stream, &format);
    Fluxion_Stream_SerializeU32(&stream, &pixelBytes);

    if (Fluxion_Stream_HasOverflowed(&stream)) return false;

    if (!Fluxion_TextureAsset_DescribesSomethingReal(width, height, mipCount, arrayLayers, (FluxionRHIFormat)format))
    {
        FLUXION_LOG_ERROR(FLUXION_TEXTURE_ASSET_LOG_CATEGORY, "a texture describes a shape or a format that cannot exist");
        return false;
    }

    // The file says how many bytes it has; the size and format say how
    // many it should have. Both are checked, and against each other:
    // believing either one alone is how a damaged file gets an allocation
    // sized from a number nothing verified.
    const usize expected = Fluxion_TextureAsset_GetTotalByteSize((FluxionRHIFormat)format, width, height, mipCount, arrayLayers);
    if (expected == 0 || (usize)pixelBytes != expected) return false;
    if ((usize)pixelBytes > size - Fluxion_Stream_GetPosition(&stream)) return false;

    const usize headerSize = sizeof(FluxionTextureAssetBlock);
    const usize totalSize = headerSize + (usize)pixelBytes;

    FluxionAllocator* allocator = Fluxion_DefaultAllocator();
    FluxionTextureAssetBlock* block = (FluxionTextureAssetBlock*)Fluxion_Allocator_Alloc(allocator, totalSize, FLUXION_DEFAULT_ALIGNMENT);
    if (!block) return false;

    memset(block, 0, headerSize);
    block->totalSize = totalSize;

    u8* pixels = (u8*)block + headerSize;
    Fluxion_Stream_SerializeBytes(&stream, pixels, pixelBytes);

    if (Fluxion_Stream_HasOverflowed(&stream))
    {
        Fluxion_Allocator_Free(allocator, block, totalSize);
        return false;
    }

    block->asset.width = width;
    block->asset.height = height;
    block->asset.mipCount = mipCount;
    block->asset.arrayLayers = arrayLayers;
    block->asset.format = (FluxionRHIFormat)format;
    block->asset.pixels = pixels;
    block->asset.pixelBytes = pixelBytes;

    // Said outright: there is no texture yet, and index zero is a real
    // texture. Leaving these zeroed would make an unfinished asset look
    // like one holding whatever was handed out first.
    block->asset.texture = (FluxionRHITextureHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };
    block->asset.view = (FluxionRHITextureViewHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    *outAsset = &block->asset;
    return true;
}

void Fluxion_TextureAsset_Destroy(FluxionTextureAsset* asset)
{
    if (!asset) return;

    FluxionTextureAssetBlock* block = (FluxionTextureAssetBlock*)asset;

    if (FLUXION_HANDLE_IS_VALID(asset->view)) Fluxion_RHI_DestroyTextureView(asset->view);
    if (FLUXION_HANDLE_IS_VALID(asset->texture)) Fluxion_RHI_DestroyTexture(asset->texture);

    Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), block, block->totalSize);
}

// ---------------------------------------------------------------------
// The upload.
// ---------------------------------------------------------------------

static usize Fluxion_TextureAsset_AlignUp(usize value, usize alignment)
{
    const usize remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

// Where each level sits in the staging buffer.
//
// A cooked file packs its levels tightly, because that is what a file
// wants. A device wants every row a fixed distance apart and every level
// beginning on a fixed boundary, and those are not the same layout. The
// re-laying-out happens here, once, at the only point where the two
// meet -- and it is why a texture whose rows already happen to satisfy
// both (a 256-byte-wide one, say) is exactly the texture that would hide
// a mistake here.
typedef struct FluxionTextureAssetPlacement
{
    usize sourceOffset;
    usize stagingOffset;
    usize sourceRowBytes;
    usize stagingRowBytes;
    u32 rows;
} FluxionTextureAssetPlacement;

static usize Fluxion_TextureAsset_PlanUpload(const FluxionTextureAsset* asset, FluxionTextureAssetPlacement* placements, u32 capacity, u32* outCount)
{
    const u32 bytesPerPixel = Fluxion_TextureAsset_BytesPerPixel(asset->format);
    usize sourceOffset = 0;
    usize stagingOffset = 0;
    u32 count = 0;

    for (u32 layer = 0; layer < asset->arrayLayers; ++layer)
    {
        for (u32 level = 0; level < asset->mipCount; ++level)
        {
            if (count >= capacity) return 0;

            const u32 levelWidth = Fluxion_TextureAsset_GetLevelExtent(asset->width, level);
            const u32 levelHeight = Fluxion_TextureAsset_GetLevelExtent(asset->height, level);

            const usize sourceRowBytes = (usize)levelWidth * bytesPerPixel;
            const usize stagingRowBytes = Fluxion_TextureAsset_AlignUp(sourceRowBytes, FLUXION_RHI_TEXTURE_DATA_ROW_ALIGNMENT);

            stagingOffset = Fluxion_TextureAsset_AlignUp(stagingOffset, FLUXION_RHI_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

            placements[count].sourceOffset = sourceOffset;
            placements[count].stagingOffset = stagingOffset;
            placements[count].sourceRowBytes = sourceRowBytes;
            placements[count].stagingRowBytes = stagingRowBytes;
            placements[count].rows = levelHeight;
            ++count;

            sourceOffset += sourceRowBytes * levelHeight;
            stagingOffset += stagingRowBytes * levelHeight;
        }
    }

    *outCount = count;
    return stagingOffset;
}

static bool Fluxion_TextureAsset_Upload(FluxionTextureAsset* asset, const FluxionTextureAssetContext* context)
{
    FluxionTextureAssetPlacement placements[FLUXION_TEXTURE_ASSET_MAX_MIPS * 8];
    u32 placementCount = 0;

    const usize stagingSize = Fluxion_TextureAsset_PlanUpload(asset, placements,
                                                              (u32)(sizeof(placements) / sizeof(placements[0])), &placementCount);
    if (stagingSize == 0 || placementCount == 0) return false;

    FluxionRHITextureDesc textureDesc;
    memset(&textureDesc, 0, sizeof(textureDesc));
    textureDesc.width = asset->width;
    textureDesc.height = asset->height;
    textureDesc.depth = 1;
    textureDesc.mipLevels = asset->mipCount;
    textureDesc.arrayLayers = asset->arrayLayers;
    textureDesc.sampleCount = 1;
    textureDesc.format = asset->format;
    textureDesc.usageFlags = FLUXION_RHI_TEXTURE_USAGE_SAMPLED | FLUXION_RHI_TEXTURE_USAGE_TRANSFER_DST;
    textureDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_GPU_ONLY;
    textureDesc.debugName = "Fluxion.TextureAsset";

    FluxionRHITextureHandle texture = Fluxion_RHI_CreateTexture(context->device, &textureDesc);
    if (!FLUXION_HANDLE_IS_VALID(texture)) return false;

    FluxionRHIBufferDesc stagingDesc;
    stagingDesc.size = stagingSize;
    stagingDesc.usageFlags = FLUXION_RHI_BUFFER_USAGE_TRANSFER_SRC;
    stagingDesc.memoryClass = FLUXION_RHI_MEMORY_CLASS_CPU_TO_GPU;
    stagingDesc.debugName = "Fluxion.TextureAsset.Staging";
    FluxionRHIBufferHandle staging = Fluxion_RHI_CreateBuffer(context->device, &stagingDesc);
    if (!FLUXION_HANDLE_IS_VALID(staging))
    {
        Fluxion_RHI_DestroyTexture(texture);
        return false;
    }

    {
        u8* mapped = (u8*)Fluxion_RHI_MapBuffer(staging);
        const u8* source = (const u8*)asset->pixels;

        for (u32 i = 0; i < placementCount; ++i)
        {
            const FluxionTextureAssetPlacement* placement = &placements[i];
            for (u32 row = 0; row < placement->rows; ++row)
            {
                memcpy(mapped + placement->stagingOffset + (usize)row * placement->stagingRowBytes,
                       source + placement->sourceOffset + (usize)row * placement->sourceRowBytes,
                       placement->sourceRowBytes);
            }
        }

        Fluxion_RHI_UnmapBuffer(staging);
    }

    FluxionRHICommandListHandle commandList = Fluxion_RHI_CreateCommandList(context->device, FLUXION_RHI_QUEUE_TYPE_GRAPHICS);
    Fluxion_RHI_CommandList_Begin(commandList);

    FluxionRHIBufferHandle noBuffer = { FLUXION_HANDLE_INVALID_INDEX, 0 };

    FluxionRHIBarrier toCopy;
    toCopy.texture = texture;
    toCopy.buffer = noBuffer;
    toCopy.before = FLUXION_RHI_RESOURCE_STATE_COMMON;
    toCopy.after = FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION;
    Fluxion_RHI_CommandList_Barrier(commandList, &toCopy, 1);

    {
        u32 i = 0;
        for (u32 layer = 0; layer < asset->arrayLayers; ++layer)
        {
            for (u32 level = 0; level < asset->mipCount; ++level, ++i)
            {
                Fluxion_RHI_CommandList_CopyBufferToTexture(commandList, staging, placements[i].stagingOffset, texture, level, layer);
            }
        }
    }

    FluxionRHIBarrier toRead;
    toRead.texture = texture;
    toRead.buffer = noBuffer;
    toRead.before = FLUXION_RHI_RESOURCE_STATE_COPY_DESTINATION;
    toRead.after = FLUXION_RHI_RESOURCE_STATE_SHADER_READ;
    Fluxion_RHI_CommandList_Barrier(commandList, &toRead, 1);

    Fluxion_RHI_CommandList_End(commandList);

    // Waited on rather than left in flight: the staging buffer holds the
    // only copy of these pixels, and it is destroyed on the next line.
    FluxionRHIFenceHandle fence = Fluxion_RHI_CreateFence(context->device, false);
    Fluxion_RHI_Queue_Submit(context->queue, &commandList, 1, fence);
    Fluxion_RHI_WaitForFence(fence);

    Fluxion_RHI_DestroyFence(fence);
    Fluxion_RHI_DestroyCommandList(commandList);
    Fluxion_RHI_DestroyBuffer(staging);
    Fluxion_RHI_Device_CollectGarbage(context->device);

    FluxionRHITextureViewDesc viewDesc;
    viewDesc.texture = texture;
    viewDesc.format = asset->format;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = asset->mipCount;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = asset->arrayLayers;

    FluxionRHITextureViewHandle view = Fluxion_RHI_CreateTextureView(context->device, &viewDesc);
    if (!FLUXION_HANDLE_IS_VALID(view))
    {
        Fluxion_RHI_DestroyTexture(texture);
        return false;
    }

    asset->texture = texture;
    asset->view = view;

    // A device holds the pixels now, so the copy that was read stops
    // being worth pointing at. The memory belongs to the one block and
    // goes when that does.
    asset->pixels = NULL;
    asset->pixelBytes = 0;

    return true;
}

// ---------------------------------------------------------------------
// The asset type.
// ---------------------------------------------------------------------

static bool Fluxion_TextureAsset_Load(const u8* bytes, usize size, void** outObject, void* userData)
{
    FLUXION_UNUSED(userData);

    FluxionTextureAsset* asset = NULL;
    if (!Fluxion_TextureAsset_Read(bytes, size, &asset)) return false;

    *outObject = asset;
    return true;
}

bool FluxionRendererInternal_TextureAsset_Upload(struct FluxionTextureAsset* asset, FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue)
{
    FluxionTextureAssetContext context;
    context.device = device;
    context.queue = queue;
    return Fluxion_TextureAsset_Upload(asset, &context);
}

static bool Fluxion_TextureAsset_Finalize(void* object, void* userData)
{
    return Fluxion_TextureAsset_Upload((FluxionTextureAsset*)object, (const FluxionTextureAssetContext*)userData);
}

static void Fluxion_TextureAsset_Unload(void* object, void* userData)
{
    FLUXION_UNUSED(userData);
    Fluxion_TextureAsset_Destroy((FluxionTextureAsset*)object);
}

bool Fluxion_TextureAsset_RegisterType(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue)
{
    if (s_registered) return true;

    s_context.device = device;
    s_context.queue = queue;

    FluxionAssetTypeDesc desc;
    memset(&desc, 0, sizeof(desc));

    memcpy(desc.name, FLUXION_TEXTURE_ASSET_TYPE_NAME, sizeof(FLUXION_TEXTURE_ASSET_TYPE_NAME));
    memcpy(desc.cookedExtension, "fluxtex", sizeof("fluxtex"));

    // No source extensions and no import function: this module holds the
    // half that ships. An importer plugin claims png/jpg/tga by
    // registering its own.
    desc.sourceExtensionCount = 0;
    desc.import = NULL;

    desc.defaultShipPolicy = FLUXION_ASSET_SHIP_COOKED;
    desc.load = Fluxion_TextureAsset_Load;
    desc.finalize = Fluxion_TextureAsset_Finalize;
    desc.unload = Fluxion_TextureAsset_Unload;
    desc.userData = &s_context;

    s_registered = Fluxion_AssetTypes_Register(&desc);
    return s_registered;
}

void Fluxion_TextureAsset_UnregisterType(void)
{
    if (!s_registered) return;

    Fluxion_AssetTypes_Unregister(Fluxion_TextureAsset_TypeId());
    s_registered = false;
}

// ---------------------------------------------------------------------
// What a texture is for.
// ---------------------------------------------------------------------

FluxionRHIFormat Fluxion_TextureAsset_GetUsageFormat(FluxionTextureUsage usage)
{
    switch (usage)
    {
        // The only one stored with an sRGB curve, because it is the only
        // one holding something a person chose by eye.
        case FLUXION_TEXTURE_USAGE_COLOR_SRGB:
            return FLUXION_RHI_FORMAT_R8G8B8A8_SRGB;

        // Linear, all of them. A normal is a direction and a mask is a
        // number; putting either through an sRGB curve does not make it
        // look wrong, it makes it BE wrong, and nothing reports that.
        case FLUXION_TEXTURE_USAGE_NORMAL_MAP:
        case FLUXION_TEXTURE_USAGE_MASK_LINEAR:
        case FLUXION_TEXTURE_USAGE_GRAYSCALE:
            return FLUXION_RHI_FORMAT_R8G8B8A8_UNORM;

        case FLUXION_TEXTURE_USAGE_HDR:
            return FLUXION_RHI_FORMAT_R16G16B16A16_FLOAT;

        default:
            return FLUXION_RHI_FORMAT_UNKNOWN;
    }
}

bool Fluxion_TextureAsset_IsUsageSRGB(FluxionTextureUsage usage)
{
    // Read off the format rather than kept beside it. Two places saying
    // the same thing is two places that can disagree, and the disagreement
    // here would be a picture that looks washed out with nothing to point
    // at.
    const FluxionRHIFormat format = Fluxion_TextureAsset_GetUsageFormat(usage);
    return format == FLUXION_RHI_FORMAT_R8G8B8A8_SRGB || format == FLUXION_RHI_FORMAT_B8G8R8A8_SRGB;
}

FluxionTextureImportSettings Fluxion_TextureAsset_DefaultImportSettings(FluxionTextureUsage usage)
{
    FluxionTextureImportSettings settings;
    memset(&settings, 0, sizeof(settings));

    settings.version = FLUXION_TEXTURE_IMPORT_SETTINGS_VERSION;
    settings.usage = (u32)usage;

    // Mips by default, for everything. Without them every surface that
    // recedes shimmers, and the shimmer moves with the camera -- which
    // reads as the renderer being broken rather than as a missing setting.
    settings.flags = FLUXION_TEXTURE_IMPORT_GENERATE_MIPS;

    settings.alphaCoverageThreshold = 0.5f;
    settings.maxSize = 0;

    settings.addressModeU = (u32)FLUXION_RHI_ADDRESS_MODE_REPEAT;
    settings.addressModeV = (u32)FLUXION_RHI_ADDRESS_MODE_REPEAT;
    settings.filter = (u32)FLUXION_RHI_FILTER_LINEAR;
    settings.maxAnisotropy = 8.0f;

    if (usage == FLUXION_TEXTURE_USAGE_NORMAL_MAP)
    {
        // A normal map is the one map whose own variation says how rough
        // the surface looks from far away, so it is also the one that can
        // do something about the shimmer.
        settings.flags |= FLUXION_TEXTURE_IMPORT_ROUGHNESS_LIMITER;
    }

    if (usage == FLUXION_TEXTURE_USAGE_HDR)
    {
        // An environment is sampled by direction, and wrapping one at the
        // seam samples the far side of the sky.
        settings.addressModeU = (u32)FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
        settings.addressModeV = (u32)FLUXION_RHI_ADDRESS_MODE_CLAMP_TO_EDGE;
    }

    return settings;
}

FluxionRHISamplerDesc Fluxion_TextureAsset_GetSamplerDesc(const FluxionTextureImportSettings* settings)
{
    FluxionRHISamplerDesc desc;
    memset(&desc, 0, sizeof(desc));

    desc.minFilter = FLUXION_RHI_FILTER_LINEAR;
    desc.magFilter = FLUXION_RHI_FILTER_LINEAR;
    desc.mipFilter = FLUXION_RHI_FILTER_LINEAR;
    desc.addressModeU = FLUXION_RHI_ADDRESS_MODE_REPEAT;
    desc.addressModeV = FLUXION_RHI_ADDRESS_MODE_REPEAT;
    desc.addressModeW = FLUXION_RHI_ADDRESS_MODE_REPEAT;
    desc.maxAnisotropy = 1.0f;
    desc.debugName = NULL;

    if (settings == NULL) return desc;

    desc.minFilter = (FluxionRHIFilter)settings->filter;
    desc.magFilter = (FluxionRHIFilter)settings->filter;
    desc.mipFilter = (FluxionRHIFilter)settings->filter;
    desc.addressModeU = (FluxionRHIAddressMode)settings->addressModeU;
    desc.addressModeV = (FluxionRHIAddressMode)settings->addressModeV;
    desc.addressModeW = (FluxionRHIAddressMode)settings->addressModeV;
    desc.maxAnisotropy = settings->maxAnisotropy;

    // Anisotropy only means anything with mips to be anisotropic across.
    if ((settings->flags & FLUXION_TEXTURE_IMPORT_GENERATE_MIPS) == 0) desc.maxAnisotropy = 1.0f;

    return desc;
}
