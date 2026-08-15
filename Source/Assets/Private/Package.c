#include <Fluxion/Assets/Package.h>

#include <Fluxion/Assets/AssetDatabase.h>
#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Containers/DynamicArray.h>
#include <Fluxion/Foundation/Containers/HashMap.h>
#include <Fluxion/Foundation/Hashing.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/Serialization/Stream.h>
#include <Fluxion/Platform/File.h>

#include <string.h>

#define FLUXION_PACKAGE_LOG_CATEGORY "Package"

// magic + format version + entry count.
#define FLUXION_PACKAGE_HEADER_SIZE 12

// Each index entry carries three fixed-width numbers after its path.
#define FLUXION_PACKAGE_ENTRY_FIXED_SIZE (4u + 8u + 8u + 8u)

// Where reading the index starts. The index of a package with thousands
// of entries fits comfortably inside this, and a package whose index does
// not is read again with more room rather than in full -- the blobs can
// be far larger than anything worth holding in memory to find a path.
#define FLUXION_PACKAGE_INDEX_READ_CHUNK (1024u * 1024u)

typedef struct FluxionPackageEntry
{
    u32 pathOffset; // into the source's text pool
    u64 blobOffset; // from the start of the file
    u64 blobSize;
    u64 blobHash;
} FluxionPackageEntry;

// ---------------------------------------------------------------------
// Reading.
// ---------------------------------------------------------------------

typedef struct FluxionPackageSource
{
    FluxionVfsSource base;

    char* realPath;
    usize realPathCapacity;

    FluxionPackageEntry* entries;
    u32 entryCount;

    char* text;
    usize textSize;
} FluxionPackageSource;

static const char* Fluxion_PackageSource_PathAt(const FluxionPackageSource* self, u32 offset)
{
    return (offset < self->textSize) ? self->text + offset : "";
}

static const FluxionPackageEntry* Fluxion_PackageSource_Find(const FluxionPackageSource* self, const char* path)
{
    // Linear. A package holds thousands of entries at most, and every
    // lookup here is followed by opening a file, which costs far more
    // than the comparisons do.
    for (u32 i = 0; i < self->entryCount; ++i)
    {
        if (strcmp(Fluxion_PackageSource_PathAt(self, self->entries[i].pathOffset), path) == 0) return &self->entries[i];
    }
    return NULL;
}

static bool Fluxion_Package_ReadExactly(FluxionFile* file, u8* buffer, usize size)
{
    usize filled = 0;
    while (filled < size)
    {
        const usize read = Fluxion_Platform_FileRead(file, buffer + filled, size - filled);
        if (read == 0) return false;
        filled += read;
    }
    return true;
}

static bool Fluxion_PackageSource_Exists(FluxionVfsSource* source, const char* path)
{
    const FluxionPackageSource* self = (const FluxionPackageSource*)source;
    return Fluxion_PackageSource_Find(self, path) != NULL;
}

static u8* Fluxion_PackageSource_ReadAll(FluxionVfsSource* source, const char* path, usize* outSize)
{
    const FluxionPackageSource* self = (const FluxionPackageSource*)source;

    const FluxionPackageEntry* entry = Fluxion_PackageSource_Find(self, path);
    if (!entry) return NULL;

    FluxionAllocator* allocator = Fluxion_Vfs_GetAllocator();
    const usize size = (usize)entry->blobSize;
    const usize allocated = size > 0 ? size : 1u;

    u8* bytes = (u8*)Fluxion_Allocator_Alloc(allocator, allocated, FLUXION_DEFAULT_ALIGNMENT);
    if (!bytes) return NULL;

    // Opened per read rather than kept open, so several threads may read
    // from one package at the same time without sharing a position --
    // which is exactly what loading assets does.
    FluxionFile file;
    if (!Fluxion_Platform_FileOpen(&file, self->realPath, FLUXION_FILE_OPEN_READ))
    {
        Fluxion_Allocator_Free(allocator, bytes, allocated);
        return NULL;
    }

    bool ok = Fluxion_Platform_FileSeek(&file, (i64)entry->blobOffset);
    if (ok && size > 0) ok = Fluxion_Package_ReadExactly(&file, bytes, size);
    Fluxion_Platform_FileClose(&file);

    // Checked, not assumed. A package cut short, or written over while it
    // was being read, otherwise hands back bytes that look like an asset
    // and are not one -- and that fails somewhere else entirely, long
    // after the thing that went wrong.
    if (ok && Fluxion_HashBytes64(bytes, size) != entry->blobHash)
    {
        FLUXION_LOG_ERROR(FLUXION_PACKAGE_LOG_CATEGORY, "'%s' is not what the package says it should be", path);
        ok = false;
    }

    if (!ok)
    {
        Fluxion_Allocator_Free(allocator, bytes, allocated);
        return NULL;
    }

    if (outSize) *outSize = size;
    return bytes;
}

static void Fluxion_PackageSource_Destroy(FluxionVfsSource* source)
{
    FluxionPackageSource* self = (FluxionPackageSource*)source;
    FluxionAllocator* allocator = Fluxion_Vfs_GetAllocator();

    if (self->entries) Fluxion_Allocator_Free(allocator, self->entries, self->entryCount * sizeof(FluxionPackageEntry));
    if (self->text) Fluxion_Allocator_Free(allocator, self->text, self->textSize);
    if (self->realPath) Fluxion_Allocator_Free(allocator, self->realPath, self->realPathCapacity);
    Fluxion_Allocator_Free(allocator, self, sizeof(FluxionPackageSource));
}

static const FluxionVfsSourceVTable s_packageVTable = {
    Fluxion_PackageSource_Exists,
    Fluxion_PackageSource_ReadAll,
    NULL, // A package is what shipped. Nothing writes into it.
    Fluxion_PackageSource_Destroy,
};

// Walks the index without storing it, to find out how much room the paths
// need and whether the whole index is even present in `buffer`.
static bool Fluxion_Package_MeasureIndex(const u8* buffer, usize size, i64 fileSize,
                                         u32* outEntryCount, usize* outTextSize, bool* outNeedsMore)
{
    *outEntryCount = 0;
    *outTextSize = 0;
    *outNeedsMore = false;

    FluxionStream stream;
    Fluxion_MemoryStream_InitReader(&stream, buffer, size);

    u32 magic = 0;
    u32 formatVersion = 0;
    u32 entryCount = 0;
    Fluxion_Stream_SerializeU32(&stream, &magic);
    Fluxion_Stream_SerializeU32(&stream, &formatVersion);
    Fluxion_Stream_SerializeU32(&stream, &entryCount);

    if (Fluxion_Stream_HasOverflowed(&stream)) return false;
    if (magic != FLUXION_PACKAGE_FILE_MAGIC) return false;
    if (formatVersion > FLUXION_PACKAGE_FORMAT_VERSION)
    {
        FLUXION_LOG_ERROR(FLUXION_PACKAGE_LOG_CATEGORY,
                          "package was written by a newer build (version %u); refusing to read it", formatVersion);
        return false;
    }

    usize textSize = 0;

    for (u32 i = 0; i < entryCount; ++i)
    {
        u32 pathLength = 0;
        Fluxion_Stream_SerializeU32(&stream, &pathLength);
        Fluxion_Stream_Skip(&stream, pathLength);
        Fluxion_Stream_Skip(&stream, FLUXION_PACKAGE_ENTRY_FIXED_SIZE - 4u);

        if (Fluxion_Stream_HasOverflowed(&stream))
        {
            // Running out of buffer means one of two things, and only one
            // of them is worth trying again for: there may be more file
            // left that was not read, or there may not.
            *outNeedsMore = (usize)fileSize > size;
            return false;
        }

        // A path longer than the whole file is damage, not a short read.
        if ((u64)pathLength > (u64)fileSize) return false;

        textSize += (usize)pathLength + 1u;
    }

    *outEntryCount = entryCount;
    *outTextSize = textSize > 0 ? textSize : 1u;
    return true;
}

FluxionVfsSource* Fluxion_VfsPackageSource_CreateFromFile(const char* realPath)
{
    if (!Fluxion_Vfs_IsInitialized() || !realPath) return NULL;

    FluxionAllocator* allocator = Fluxion_Vfs_GetAllocator();

    FluxionFile file;
    if (!Fluxion_Platform_FileOpen(&file, realPath, FLUXION_FILE_OPEN_READ))
    {
        FLUXION_LOG_ERROR(FLUXION_PACKAGE_LOG_CATEGORY, "cannot open '%s'", realPath);
        return NULL;
    }

    const i64 fileSize = Fluxion_Platform_FileSize(&file);
    if (fileSize < (i64)FLUXION_PACKAGE_HEADER_SIZE)
    {
        Fluxion_Platform_FileClose(&file);
        return NULL;
    }

    // Only as much of the file as the index needs is read. A package's
    // blobs can be far larger than anything worth holding in memory just
    // to find out what paths are in it.
    usize capacity = (usize)fileSize < FLUXION_PACKAGE_INDEX_READ_CHUNK ? (usize)fileSize : FLUXION_PACKAGE_INDEX_READ_CHUNK;
    u8* buffer = NULL;
    u32 entryCount = 0;
    usize textSize = 0;
    bool ok = false;

    for (;;)
    {
        buffer = (u8*)Fluxion_Allocator_Alloc(allocator, capacity, FLUXION_DEFAULT_ALIGNMENT);
        if (!buffer) break;

        if (!Fluxion_Platform_FileSeek(&file, 0) || !Fluxion_Package_ReadExactly(&file, buffer, capacity)) break;

        bool needsMore = false;
        if (Fluxion_Package_MeasureIndex(buffer, capacity, fileSize, &entryCount, &textSize, &needsMore))
        {
            ok = true;
            break;
        }

        if (!needsMore || capacity >= (usize)fileSize) break;

        Fluxion_Allocator_Free(allocator, buffer, capacity);
        buffer = NULL;

        const usize doubled = capacity * 2u;
        capacity = doubled > (usize)fileSize ? (usize)fileSize : doubled;
    }

    Fluxion_Platform_FileClose(&file);

    if (!ok)
    {
        if (buffer) Fluxion_Allocator_Free(allocator, buffer, capacity);
        FLUXION_LOG_ERROR(FLUXION_PACKAGE_LOG_CATEGORY, "'%s' is not a package this build can read", realPath);
        return NULL;
    }

    FluxionPackageSource* self = (FluxionPackageSource*)Fluxion_Allocator_Alloc(allocator, sizeof(FluxionPackageSource), FLUXION_DEFAULT_ALIGNMENT);
    if (self)
    {
        memset(self, 0, sizeof(*self));
        self->base.vtable = &s_packageVTable;
        self->entryCount = entryCount;
        self->textSize = textSize;

        const usize pathLength = strlen(realPath);
        self->realPathCapacity = pathLength + 1;
        self->realPath = (char*)Fluxion_Allocator_Alloc(allocator, self->realPathCapacity, FLUXION_DEFAULT_ALIGNMENT);
        self->text = (char*)Fluxion_Allocator_Alloc(allocator, textSize, FLUXION_DEFAULT_ALIGNMENT);
        self->entries = entryCount > 0
                            ? (FluxionPackageEntry*)Fluxion_Allocator_Alloc(allocator, entryCount * sizeof(FluxionPackageEntry), FLUXION_DEFAULT_ALIGNMENT)
                            : NULL;

        if (!self->realPath || !self->text || (entryCount > 0 && !self->entries))
        {
            ok = false;
        }
        else
        {
            memcpy(self->realPath, realPath, self->realPathCapacity);
            memset(self->text, 0, textSize);
        }
    }
    else
    {
        ok = false;
    }

    if (ok)
    {
        FluxionStream stream;
        Fluxion_MemoryStream_InitReader(&stream, buffer, capacity);
        Fluxion_Stream_Skip(&stream, FLUXION_PACKAGE_HEADER_SIZE);

        u32 textUsed = 0;
        for (u32 i = 0; i < entryCount; ++i)
        {
            u32 pathLength = 0;
            Fluxion_Stream_SerializeU32(&stream, &pathLength);

            self->entries[i].pathOffset = textUsed;
            Fluxion_Stream_SerializeBytes(&stream, self->text + textUsed, pathLength);
            self->text[textUsed + pathLength] = '\0';
            textUsed += pathLength + 1;

            Fluxion_Stream_SerializeU64(&stream, &self->entries[i].blobOffset);
            Fluxion_Stream_SerializeU64(&stream, &self->entries[i].blobSize);
            Fluxion_Stream_SerializeU64(&stream, &self->entries[i].blobHash);

            // An entry pointing past the end of the file is not an entry.
            // Said here once, so that no read below has to wonder.
            if (self->entries[i].blobOffset + self->entries[i].blobSize > (u64)fileSize)
            {
                FLUXION_LOG_ERROR(FLUXION_PACKAGE_LOG_CATEGORY, "'%s' has an entry pointing outside the file", realPath);
                ok = false;
                break;
            }
        }

        if (Fluxion_Stream_HasOverflowed(&stream)) ok = false;
    }

    Fluxion_Allocator_Free(allocator, buffer, capacity);

    if (!ok)
    {
        if (self) Fluxion_PackageSource_Destroy(&self->base);
        return NULL;
    }

    return &self->base;
}

// ---------------------------------------------------------------------
// Building.
// ---------------------------------------------------------------------

FluxionAssetShipPolicy Fluxion_Package_ResolveShipPolicy(const FluxionPackageBuildDesc* desc, const FluxionAssetTypeDesc* type)
{
    if (!type) return FLUXION_ASSET_SHIP_NEVER;

    if (desc && desc->overrides)
    {
        for (u32 i = 0; i < desc->overrideCount; ++i)
        {
            if (strcmp(desc->overrides[i].typeName, type->name) == 0) return desc->overrides[i].policy;
        }
    }

    return type->defaultShipPolicy;
}

typedef struct FluxionPackageBuildItem
{
    bool included;

    // Both into the builder's text pool. The first is the path inside the
    // package, the second the full path the bytes are read from now.
    u32 relativePathOffset;
    u32 fullPathOffset;
} FluxionPackageBuildItem;

typedef struct FluxionPackageBuilder
{
    FluxionDynamicArray text;    // char, offset 0 is the empty string
    FluxionDynamicArray items;   // FluxionPackageBuildItem
    FluxionHashMap shipPathById; // FluxionUUID -> u32 text offset of the shipped path
} FluxionPackageBuilder;

static u32 Fluxion_PackageBuilder_PushText(FluxionPackageBuilder* builder, const char* text)
{
    if (!text || text[0] == '\0') return 0;

    const u32 offset = (u32)builder->text.count;
    const usize length = strlen(text);
    for (usize i = 0; i <= length; ++i) Fluxion_DynamicArray_Push(&builder->text, &text[i]);
    return offset;
}

static const char* Fluxion_PackageBuilder_TextAt(FluxionPackageBuilder* builder, u32 offset)
{
    if (offset >= builder->text.count) return "";
    return (const char*)Fluxion_DynamicArray_At(&builder->text, offset);
}

static bool Fluxion_PackageBuilder_ShouldWrite(const FluxionAssetRecord* record, void* userData)
{
    FluxionPackageBuilder* builder = (FluxionPackageBuilder*)userData;
    return Fluxion_HashMap_Find(&builder->shipPathById, &record->id) != NULL;
}

static const char* Fluxion_PackageBuilder_CookedPathFor(const FluxionAssetRecord* record, void* userData)
{
    FluxionPackageBuilder* builder = (FluxionPackageBuilder*)userData;

    const u32* offset = (const u32*)Fluxion_HashMap_Find(&builder->shipPathById, &record->id);
    return offset ? Fluxion_PackageBuilder_TextAt(builder, *offset) : "";
}

static void Fluxion_Package_CopyBounded(char* destination, usize capacity, const char* source)
{
    const usize length = strlen(source);
    const usize copied = length < capacity ? length : capacity - 1;
    memcpy(destination, source, copied);
    destination[copied] = '\0';
}

static void Fluxion_Package_NoteOutcome(FluxionPackageBuildEntry* entry, const FluxionAssetRecord* record,
                                        const FluxionAssetTypeDesc* type, FluxionPackageOutcome outcome)
{
    memset(entry, 0, sizeof(*entry));
    entry->asset = record->id;
    entry->outcome = outcome;
    Fluxion_Package_CopyBounded(entry->name, sizeof(entry->name), Fluxion_AssetDatabase_GetName(record));
    if (type) Fluxion_Package_CopyBounded(entry->typeName, sizeof(entry->typeName), type->name);
}

static bool Fluxion_Package_WriteFile(const char* realPath, const u8* bytes, usize size)
{
    FluxionFile file;
    if (!Fluxion_Platform_FileOpen(&file, realPath, FLUXION_FILE_OPEN_WRITE)) return false;

    bool ok = true;
    usize written = 0;
    while (written < size)
    {
        const usize step = Fluxion_Platform_FileWrite(&file, bytes + written, size - written);
        if (step == 0)
        {
            ok = false;
            break;
        }
        written += step;
    }

    Fluxion_Platform_FileClose(&file);
    return ok;
}

// Serialises the shipped index, growing the buffer until it fits. The
// caller frees what comes back with the capacity it is given.
static u8* Fluxion_Package_BuildIndexBlob(FluxionPackageBuilder* builder, FluxionAllocator* allocator,
                                          usize* outCapacity, usize* outSize)
{
    usize capacity = 64u * 1024u;

    for (;;)
    {
        u8* bytes = (u8*)Fluxion_Allocator_Alloc(allocator, capacity, FLUXION_DEFAULT_ALIGNMENT);
        if (!bytes) return NULL;

        FluxionStream stream;
        Fluxion_MemoryStream_InitWriter(&stream, bytes, capacity);

        FluxionAssetDatabaseWriteFilter filter;
        memset(&filter, 0, sizeof(filter));
        filter.shouldWrite = Fluxion_PackageBuilder_ShouldWrite;
        filter.cookedPathFor = Fluxion_PackageBuilder_CookedPathFor;

        // The whole point. A shipped index has nowhere for a source path
        // to be read from, so it does not carry one.
        filter.includeSourcePaths = false;
        filter.userData = builder;

        if (Fluxion_AssetDatabase_SerializeFiltered(&stream, &filter))
        {
            *outCapacity = capacity;
            *outSize = Fluxion_Stream_GetPosition(&stream);
            return bytes;
        }

        Fluxion_Allocator_Free(allocator, bytes, capacity);

        if (capacity > 256u * 1024u * 1024u) return NULL;
        capacity *= 2u;
    }
}

bool Fluxion_Package_Build(const FluxionPackageBuildDesc* desc, const char* outputRealPath, FluxionPackageBuildReport* outReport)
{
    if (outReport) memset(outReport, 0, sizeof(*outReport));

    if (!Fluxion_Vfs_IsInitialized() || !Fluxion_AssetDatabase_IsInitialized() || !Fluxion_AssetTypes_IsInitialized() || !outputRealPath)
    {
        return false;
    }

    FluxionAllocator* allocator = Fluxion_Vfs_GetAllocator();
    const u32 assetCount = Fluxion_AssetDatabase_GetCount();
    const char* scheme = (desc && desc->scheme[0] != '\0') ? desc->scheme : "assets";

    FluxionPackageBuilder builder;
    Fluxion_DynamicArray_Init(&builder.text, allocator, sizeof(char));
    Fluxion_DynamicArray_Init(&builder.items, allocator, sizeof(FluxionPackageBuildItem));
    Fluxion_HashMap_Init(&builder.shipPathById, allocator, sizeof(FluxionUUID), sizeof(u32), Fluxion_HashBytes64, Fluxion_BytesEqual);

    const char terminator = '\0';
    Fluxion_DynamicArray_Push(&builder.text, &terminator);

    FluxionPackageBuildEntry* entries = NULL;
    if (assetCount > 0)
    {
        entries = (FluxionPackageBuildEntry*)Fluxion_Allocator_Alloc(allocator, assetCount * sizeof(FluxionPackageBuildEntry), FLUXION_DEFAULT_ALIGNMENT);
        if (!entries)
        {
            Fluxion_HashMap_Destroy(&builder.shipPathById);
            Fluxion_DynamicArray_Destroy(&builder.items);
            Fluxion_DynamicArray_Destroy(&builder.text);
            return false;
        }
        memset(entries, 0, assetCount * sizeof(FluxionPackageBuildEntry));
    }

    u32 includedCount = 0;
    u32 excludedCount = 0;
    u32 errorCount = 0;

    // Pass one: what each asset's type says, and whether the bytes it
    // would ship are actually there.
    for (u32 i = 0; i < assetCount; ++i)
    {
        const FluxionAssetRecord* record = Fluxion_AssetDatabase_GetAt(i);
        const FluxionAssetTypeDesc* type = Fluxion_AssetTypes_Find(record->type);

        FluxionPackageBuildItem item;
        memset(&item, 0, sizeof(item));

        if (!type)
        {
            Fluxion_Package_NoteOutcome(&entries[i], record, NULL, FLUXION_PACKAGE_OUTCOME_UNKNOWN_TYPE);
            ++errorCount;
            Fluxion_DynamicArray_Push(&builder.items, &item);
            continue;
        }

        const FluxionAssetShipPolicy policy = Fluxion_Package_ResolveShipPolicy(desc, type);

        if (policy == FLUXION_ASSET_SHIP_NEVER)
        {
            Fluxion_Package_NoteOutcome(&entries[i], record, type, FLUXION_PACKAGE_OUTCOME_EXCLUDED);
            ++excludedCount;
            Fluxion_DynamicArray_Push(&builder.items, &item);
            continue;
        }

        const char* fullPath = (policy == FLUXION_ASSET_SHIP_SOURCE) ? Fluxion_AssetDatabase_GetSourcePath(record)
                                                                    : Fluxion_AssetDatabase_GetCookedPath(record);

        char pathScheme[FLUXION_VFS_MAX_SCHEME_LENGTH + 1];
        char relative[FLUXION_VFS_MAX_PATH];

        if (fullPath[0] == '\0' || !Fluxion_Vfs_SplitPath(fullPath, pathScheme, sizeof(pathScheme), relative, sizeof(relative)))
        {
            Fluxion_Package_NoteOutcome(&entries[i], record, type, FLUXION_PACKAGE_OUTCOME_MISSING_DATA);
            ++errorCount;
            Fluxion_DynamicArray_Push(&builder.items, &item);
            continue;
        }

        if (strcmp(pathScheme, scheme) != 0)
        {
            Fluxion_Package_NoteOutcome(&entries[i], record, type, FLUXION_PACKAGE_OUTCOME_WRONG_SCHEME);
            ++errorCount;
            Fluxion_DynamicArray_Push(&builder.items, &item);
            continue;
        }

        if (!Fluxion_Vfs_Exists(fullPath))
        {
            Fluxion_Package_NoteOutcome(&entries[i], record, type, FLUXION_PACKAGE_OUTCOME_MISSING_DATA);
            ++errorCount;
            Fluxion_DynamicArray_Push(&builder.items, &item);
            continue;
        }

        item.included = true;
        item.relativePathOffset = Fluxion_PackageBuilder_PushText(&builder, relative);
        item.fullPathOffset = Fluxion_PackageBuilder_PushText(&builder, fullPath);
        Fluxion_DynamicArray_Push(&builder.items, &item);

        // The shipped index has to say where the bytes ended up, which is
        // not always where they came from: an asset shipped in its source
        // form has no cooked path at all.
        {
            const u32 shippedOffset = item.fullPathOffset;
            Fluxion_HashMap_Set(&builder.shipPathById, &record->id, &shippedOffset);
        }

        Fluxion_Package_NoteOutcome(&entries[i], record, type,
                                    policy == FLUXION_ASSET_SHIP_SOURCE ? FLUXION_PACKAGE_OUTCOME_INCLUDED_SOURCE
                                                                        : FLUXION_PACKAGE_OUTCOME_INCLUDED_COOKED);
        ++includedCount;
    }

    // Pass two: an asset that ships and needs something that does not is
    // a game that starts and then does not work. This is the last moment
    // at which that can still be said out loud.
    for (u32 i = 0; i < assetCount; ++i)
    {
        const FluxionPackageBuildItem* item = (const FluxionPackageBuildItem*)Fluxion_DynamicArray_At(&builder.items, i);
        if (!item->included) continue;

        const FluxionAssetRecord* record = Fluxion_AssetDatabase_GetAt(i);

        u32 dependencyCount = 0;
        const FluxionUUID* dependencies = Fluxion_AssetDatabase_GetDependencies(record, &dependencyCount);

        for (u32 d = 0; d < dependencyCount; ++d)
        {
            if (Fluxion_HashMap_Find(&builder.shipPathById, &dependencies[d])) continue;

            entries[i].outcome = FLUXION_PACKAGE_OUTCOME_BROKEN_DEPENDENCY;
            entries[i].culprit = dependencies[d];
            --includedCount;
            ++errorCount;
            break;
        }
    }

    bool ok = (errorCount == 0);

    if (ok)
    {
        FluxionDynamicArray blobPaths; // u32 text offsets, the path inside the package
        FluxionDynamicArray blobBytes; // u8*
        FluxionDynamicArray blobSizes; // usize
        Fluxion_DynamicArray_Init(&blobPaths, allocator, sizeof(u32));
        Fluxion_DynamicArray_Init(&blobBytes, allocator, sizeof(u8*));
        Fluxion_DynamicArray_Init(&blobSizes, allocator, sizeof(usize));

        usize indexCapacity = 0;
        usize indexSize = 0;
        u8* indexBytes = Fluxion_Package_BuildIndexBlob(&builder, allocator, &indexCapacity, &indexSize);
        if (!indexBytes) ok = false;

        for (u32 i = 0; ok && i < assetCount; ++i)
        {
            const FluxionPackageBuildItem* item = (const FluxionPackageBuildItem*)Fluxion_DynamicArray_At(&builder.items, i);
            if (!item->included) continue;

            usize size = 0;
            u8* bytes = Fluxion_Vfs_ReadAll(Fluxion_PackageBuilder_TextAt(&builder, item->fullPathOffset), &size);
            if (!bytes)
            {
                ok = false;
                break;
            }

            Fluxion_DynamicArray_Push(&blobPaths, &item->relativePathOffset);
            Fluxion_DynamicArray_Push(&blobBytes, &bytes);
            Fluxion_DynamicArray_Push(&blobSizes, &size);
        }

        if (ok)
        {
            const u32 databasePathOffset = Fluxion_PackageBuilder_PushText(&builder, FLUXION_PACKAGE_DATABASE_PATH);
            Fluxion_DynamicArray_Push(&blobPaths, &databasePathOffset);
            Fluxion_DynamicArray_Push(&blobBytes, &indexBytes);
            Fluxion_DynamicArray_Push(&blobSizes, &indexSize);
        }

        u8* output = NULL;
        usize outputSize = 0;

        if (ok)
        {
            const u32 blobCount = (u32)blobPaths.count;

            usize headerSize = FLUXION_PACKAGE_HEADER_SIZE;
            usize totalBlobSize = 0;
            for (u32 i = 0; i < blobCount; ++i)
            {
                const u32 pathOffset = *(const u32*)Fluxion_DynamicArray_At(&blobPaths, i);
                headerSize += FLUXION_PACKAGE_ENTRY_FIXED_SIZE + strlen(Fluxion_PackageBuilder_TextAt(&builder, pathOffset));
                totalBlobSize += *(const usize*)Fluxion_DynamicArray_At(&blobSizes, i);
            }

            outputSize = headerSize + totalBlobSize;
            output = (u8*)Fluxion_Allocator_Alloc(allocator, outputSize > 0 ? outputSize : 1u, FLUXION_DEFAULT_ALIGNMENT);
            if (!output) ok = false;

            if (ok)
            {
                FluxionStream stream;
                Fluxion_MemoryStream_InitWriter(&stream, output, outputSize);

                u32 magic = FLUXION_PACKAGE_FILE_MAGIC;
                u32 formatVersion = FLUXION_PACKAGE_FORMAT_VERSION;
                u32 count = blobCount;
                Fluxion_Stream_SerializeU32(&stream, &magic);
                Fluxion_Stream_SerializeU32(&stream, &formatVersion);
                Fluxion_Stream_SerializeU32(&stream, &count);

                u64 runningOffset = (u64)headerSize;

                for (u32 i = 0; i < blobCount; ++i)
                {
                    const u32 pathOffset = *(const u32*)Fluxion_DynamicArray_At(&blobPaths, i);
                    const u8* bytes = *(u8* const*)Fluxion_DynamicArray_At(&blobBytes, i);
                    const usize size = *(const usize*)Fluxion_DynamicArray_At(&blobSizes, i);

                    char pathScratch[FLUXION_VFS_MAX_PATH];
                    Fluxion_Package_CopyBounded(pathScratch, sizeof(pathScratch), Fluxion_PackageBuilder_TextAt(&builder, pathOffset));

                    u32 pathLength = (u32)strlen(pathScratch);
                    u64 blobOffset = runningOffset;
                    u64 blobSize = (u64)size;
                    u64 blobHash = Fluxion_HashBytes64(bytes, size);

                    Fluxion_Stream_SerializeU32(&stream, &pathLength);
                    Fluxion_Stream_SerializeBytes(&stream, pathScratch, pathLength);
                    Fluxion_Stream_SerializeU64(&stream, &blobOffset);
                    Fluxion_Stream_SerializeU64(&stream, &blobSize);
                    Fluxion_Stream_SerializeU64(&stream, &blobHash);

                    runningOffset += blobSize;
                }

                // The offsets above were worked out from headerSize, so if
                // the index turned out to be a different length than that,
                // every one of them points at the wrong place.
                FLUXION_ASSERT_MSG(Fluxion_Stream_GetPosition(&stream) == headerSize,
                                   "the package index is not the length its own entry offsets assume");

                if (Fluxion_Stream_GetPosition(&stream) != headerSize || Fluxion_Stream_HasOverflowed(&stream)) ok = false;

                for (u32 i = 0; ok && i < blobCount; ++i)
                {
                    u8* bytes = *(u8**)Fluxion_DynamicArray_At(&blobBytes, i);
                    const usize size = *(const usize*)Fluxion_DynamicArray_At(&blobSizes, i);
                    if (size > 0) Fluxion_Stream_SerializeBytes(&stream, bytes, size);
                }

                if (Fluxion_Stream_HasOverflowed(&stream)) ok = false;
            }
        }

        if (ok) ok = Fluxion_Package_WriteFile(outputRealPath, output, outputSize);

        if (output) Fluxion_Allocator_Free(allocator, output, outputSize > 0 ? outputSize : 1u);

        for (usize i = 0; i < blobBytes.count; ++i)
        {
            u8* bytes = *(u8**)Fluxion_DynamicArray_At(&blobBytes, i);
            const usize size = *(const usize*)Fluxion_DynamicArray_At(&blobSizes, i);

            // The index blob came from the allocator, not from a read.
            if (bytes == indexBytes) continue;
            Fluxion_Vfs_FreeBuffer(bytes, size);
        }
        if (indexBytes) Fluxion_Allocator_Free(allocator, indexBytes, indexCapacity);

        Fluxion_DynamicArray_Destroy(&blobSizes);
        Fluxion_DynamicArray_Destroy(&blobBytes);
        Fluxion_DynamicArray_Destroy(&blobPaths);
    }

    if (outReport)
    {
        outReport->entries = entries;
        outReport->entryCount = assetCount;
        outReport->includedCount = includedCount;
        outReport->excludedCount = excludedCount;
        outReport->errorCount = errorCount;
    }
    else if (entries)
    {
        Fluxion_Allocator_Free(allocator, entries, assetCount * sizeof(FluxionPackageBuildEntry));
    }

    Fluxion_HashMap_Destroy(&builder.shipPathById);
    Fluxion_DynamicArray_Destroy(&builder.items);
    Fluxion_DynamicArray_Destroy(&builder.text);

    return ok;
}

void Fluxion_Package_FreeReport(FluxionPackageBuildReport* report)
{
    if (!report || !report->entries) return;

    Fluxion_Allocator_Free(Fluxion_Vfs_GetAllocator(), report->entries, report->entryCount * sizeof(FluxionPackageBuildEntry));
    memset(report, 0, sizeof(*report));
}
