#include <Fluxion/RenderCore/Renderer/MeshAsset.h>

#include <Fluxion/Assets/AssetType.h>
#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/Memory/Allocator.h>

#include <string.h>

#define FLUXION_MESH_ASSET_LOG_CATEGORY "MeshAsset"

// What the device needs, captured at registration. Static because the
// asset system holds the user data by pointer for as long as the type is
// registered, and this outlives every load that uses it.
typedef struct FluxionMeshAssetContext
{
    FluxionRHIDeviceHandle device;
    FluxionRHIQueueHandle queue;
} FluxionMeshAssetContext;

static FluxionMeshAssetContext s_context;
static bool s_registered = false;

// The mesh, its vertices and its indices in one allocation: three
// separate ones would have to be tracked separately and freed in three
// places, all of which can be forgotten one at a time.
//
// The bytes are kept rather than uploaded where they are read, because
// where they are read is a worker thread, and a device is not something a
// worker thread may touch.
typedef struct FluxionMeshAssetBlock
{
    FluxionMeshAsset asset;
    usize totalSize;
} FluxionMeshAssetBlock;

FluxionAssetTypeId Fluxion_MeshAsset_TypeId(void)
{
    return Fluxion_AssetTypeId_FromName(Fluxion_StringView_FromCStr(FLUXION_MESH_ASSET_TYPE_NAME));
}

// ---------------------------------------------------------------------
// The format, both directions in one place.
// ---------------------------------------------------------------------

static bool Fluxion_MeshAsset_SerializeLayout(FluxionStream* stream, FluxionRHIVertexLayout* layout)
{
    Fluxion_Stream_SerializeU32(stream, &layout->attributeCount);
    Fluxion_Stream_SerializeU32(stream, &layout->stride);

    if (layout->attributeCount > FLUXION_RHI_MAX_VERTEX_ATTRIBUTES)
    {
        // Refused rather than read as far as it goes: a layout with
        // attributes missing describes a different mesh, and would draw
        // as one rather than fail.
        layout->attributeCount = 0;
        return false;
    }

    for (u32 i = 0; i < layout->attributeCount; ++i)
    {
        Fluxion_Stream_SerializeU32(stream, &layout->attributes[i].location);

        u32 format = (u32)layout->attributes[i].format;
        Fluxion_Stream_SerializeU32(stream, &format);
        layout->attributes[i].format = (FluxionRHIFormat)format;

        Fluxion_Stream_SerializeU32(stream, &layout->attributes[i].offset);
    }

    return true;
}

static void Fluxion_MeshAsset_SerializeVec3(FluxionStream* stream, FluxionVec3* value)
{
    Fluxion_Stream_SerializeF32(stream, &value->x);
    Fluxion_Stream_SerializeF32(stream, &value->y);
    Fluxion_Stream_SerializeF32(stream, &value->z);
}

bool Fluxion_MeshAsset_Write(FluxionStream* stream, const FluxionMeshAssetData* data)
{
    if (!stream || !data || !Fluxion_Stream_IsWriting(stream)) return false;
    if (data->vertexDataSize == 0 || data->indexDataSize == 0) return false;

    u32 magic = FLUXION_MESH_ASSET_MAGIC;
    u32 formatVersion = FLUXION_MESH_ASSET_FORMAT_VERSION;
    Fluxion_Stream_SerializeU32(stream, &magic);
    Fluxion_Stream_SerializeU32(stream, &formatVersion);

    FluxionRHIVertexLayout layout = data->vertexLayout;
    if (!Fluxion_MeshAsset_SerializeLayout(stream, &layout)) return false;

    FluxionAABB bounds = data->bounds;
    Fluxion_MeshAsset_SerializeVec3(stream, &bounds.min);
    Fluxion_MeshAsset_SerializeVec3(stream, &bounds.max);

    u8 use16BitIndices = data->use16BitIndices ? 1u : 0u;
    Fluxion_Stream_SerializeU8(stream, &use16BitIndices);

    u32 vertexDataSize = (u32)data->vertexDataSize;
    u32 indexDataSize = (u32)data->indexDataSize;
    Fluxion_Stream_SerializeU32(stream, &vertexDataSize);
    Fluxion_Stream_SerializeU32(stream, &indexDataSize);

    // The write direction only reads from these; the stream takes one
    // pointer type for both directions.
    Fluxion_Stream_SerializeBytes(stream, (void*)data->vertexData, vertexDataSize);
    Fluxion_Stream_SerializeBytes(stream, (void*)data->indexData, indexDataSize);

    return !Fluxion_Stream_HasOverflowed(stream);
}

// ---------------------------------------------------------------------
// The load half.
// ---------------------------------------------------------------------

bool Fluxion_MeshAsset_Read(const u8* bytes, usize size, FluxionMeshAsset** outAsset)
{
    FluxionStream stream;
    Fluxion_MemoryStream_InitReader(&stream, bytes, size);

    u32 magic = 0;
    u32 formatVersion = 0;
    Fluxion_Stream_SerializeU32(&stream, &magic);
    Fluxion_Stream_SerializeU32(&stream, &formatVersion);

    if (magic != FLUXION_MESH_ASSET_MAGIC)
    {
        FLUXION_LOG_ERROR(FLUXION_MESH_ASSET_LOG_CATEGORY, "these are not the bytes of a mesh");
        return false;
    }

    if (formatVersion > FLUXION_MESH_ASSET_FORMAT_VERSION)
    {
        FLUXION_LOG_ERROR(FLUXION_MESH_ASSET_LOG_CATEGORY,
                          "mesh was written by a newer build (version %u); refusing to read it", formatVersion);
        return false;
    }

    FluxionRHIVertexLayout layout;
    memset(&layout, 0, sizeof(layout));
    if (!Fluxion_MeshAsset_SerializeLayout(&stream, &layout)) return false;

    FluxionAABB bounds;
    memset(&bounds, 0, sizeof(bounds));
    Fluxion_MeshAsset_SerializeVec3(&stream, &bounds.min);
    Fluxion_MeshAsset_SerializeVec3(&stream, &bounds.max);

    u8 use16BitIndices = 0;
    u32 vertexDataSize = 0;
    u32 indexDataSize = 0;
    Fluxion_Stream_SerializeU8(&stream, &use16BitIndices);
    Fluxion_Stream_SerializeU32(&stream, &vertexDataSize);
    Fluxion_Stream_SerializeU32(&stream, &indexDataSize);

    if (Fluxion_Stream_HasOverflowed(&stream)) return false;

    // Checked before anything is allocated. What makes the read itself
    // safe is the stream's own bounds, further down -- this is here so
    // that a damaged file claiming to hold four gigabytes does not get
    // four gigabytes asked for on its say-so before that happens.
    //
    // So the two are not one guard written twice: either alone refuses a
    // short file, and only this one refuses it without allocating first.
    const usize remaining = size - Fluxion_Stream_GetPosition(&stream);
    if ((usize)vertexDataSize + (usize)indexDataSize > remaining) return false;
    if (vertexDataSize == 0 || indexDataSize == 0) return false;

    // The mesh, its vertices and its indices in one allocation.
    const usize headerSize = sizeof(FluxionMeshAssetBlock);
    const usize totalSize = headerSize + vertexDataSize + indexDataSize;

    FluxionAllocator* allocator = Fluxion_DefaultAllocator();
    FluxionMeshAssetBlock* block = (FluxionMeshAssetBlock*)Fluxion_Allocator_Alloc(allocator, totalSize, FLUXION_DEFAULT_ALIGNMENT);
    if (!block) return false;

    memset(block, 0, headerSize);
    block->totalSize = totalSize;

    u8* vertices = (u8*)block + headerSize;
    u8* indices = vertices + vertexDataSize;

    Fluxion_Stream_SerializeBytes(&stream, vertices, vertexDataSize);
    Fluxion_Stream_SerializeBytes(&stream, indices, indexDataSize);

    if (Fluxion_Stream_HasOverflowed(&stream))
    {
        Fluxion_Allocator_Free(allocator, block, totalSize);
        return false;
    }

    block->asset.bounds = bounds;
    block->asset.vertexLayout = layout;
    block->asset.use16BitIndices = use16BitIndices != 0;
    block->asset.indexCount = use16BitIndices ? (u32)(indexDataSize / sizeof(u16)) : (u32)(indexDataSize / sizeof(u32));
    block->asset.vertexData = vertices;
    block->asset.vertexDataSize = vertexDataSize;
    block->asset.indexData = indices;
    block->asset.indexDataSize = indexDataSize;

    // Said outright: there is no buffer yet, and index zero is a real
    // buffer. Leaving this zeroed would make an unfinished mesh look like
    // one holding whatever was handed out first.
    block->asset.buffer = (FluxionMeshBufferHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    *outAsset = &block->asset;
    return true;
}

void Fluxion_MeshAsset_Destroy(FluxionMeshAsset* asset)
{
    if (!asset) return;

    FluxionMeshAssetBlock* block = (FluxionMeshAssetBlock*)asset;

    if (FLUXION_HANDLE_IS_VALID(asset->buffer)) Fluxion_MeshBuffer_Destroy(asset->buffer);
    Fluxion_Allocator_Free(Fluxion_DefaultAllocator(), block, block->totalSize);
}

static bool Fluxion_MeshAsset_Load(const u8* bytes, usize size, void** outObject, void* userData)
{
    FLUXION_UNUSED(userData);

    FluxionMeshAsset* asset = NULL;
    if (!Fluxion_MeshAsset_Read(bytes, size, &asset)) return false;

    *outObject = asset;
    return true;
}

static bool Fluxion_MeshAsset_Finalize(void* object, void* userData)
{
    FluxionMeshAsset* asset = (FluxionMeshAsset*)object;
    const FluxionMeshAssetContext* context = (const FluxionMeshAssetContext*)userData;

    FluxionMeshBufferDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.vertexData = asset->vertexData;
    desc.vertexDataSize = asset->vertexDataSize;
    desc.indexData = asset->indexData;
    desc.indexDataSize = asset->indexDataSize;
    desc.use16BitIndices = asset->use16BitIndices;
    desc.vertexLayout = asset->vertexLayout;
    desc.bounds = asset->bounds;

    asset->buffer = Fluxion_MeshBuffer_Create(context->device, context->queue, &desc);
    if (!FLUXION_HANDLE_IS_VALID(asset->buffer)) return false;

    // A device holds the mesh now, so the copy that was read stops being
    // worth pointing at. The memory itself belongs to the one block and
    // goes when that does.
    asset->vertexData = NULL;
    asset->vertexDataSize = 0;
    asset->indexData = NULL;
    asset->indexDataSize = 0;

    return true;
}

static void Fluxion_MeshAsset_Unload(void* object, void* userData)
{
    FLUXION_UNUSED(userData);
    Fluxion_MeshAsset_Destroy((FluxionMeshAsset*)object);
}

bool Fluxion_MeshAsset_RegisterType(FluxionRHIDeviceHandle device, FluxionRHIQueueHandle queue)
{
    if (s_registered) return true;

    s_context.device = device;
    s_context.queue = queue;

    FluxionAssetTypeDesc desc;
    memset(&desc, 0, sizeof(desc));

    memcpy(desc.name, FLUXION_MESH_ASSET_TYPE_NAME, sizeof(FLUXION_MESH_ASSET_TYPE_NAME));
    memcpy(desc.cookedExtension, "fluxmesh", sizeof("fluxmesh"));

    // No source extensions and no import function: this module holds the
    // half that ships, and only that half. An importer plugin claims the
    // interchange formats by registering its own type.
    desc.sourceExtensionCount = 0;
    desc.import = NULL;

    desc.defaultShipPolicy = FLUXION_ASSET_SHIP_COOKED;
    desc.load = Fluxion_MeshAsset_Load;
    desc.finalize = Fluxion_MeshAsset_Finalize;
    desc.unload = Fluxion_MeshAsset_Unload;
    desc.userData = &s_context;

    s_registered = Fluxion_AssetTypes_Register(&desc);
    return s_registered;
}

void Fluxion_MeshAsset_UnregisterType(void)
{
    if (!s_registered) return;

    Fluxion_AssetTypes_Unregister(Fluxion_MeshAsset_TypeId());
    s_registered = false;
}
