#include <Fluxion/Assets/AssetImport.h>

#include <Fluxion/Assets/AssetDatabase.h>
#include <Fluxion/Assets/AssetType.h>
#include <Fluxion/Assets/VirtualFileSystem.h>
#include <Fluxion/Core/Jobs/JobSystem.h>
#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Atomic.h>
#include <Fluxion/Foundation/Hashing.h>
#include <Fluxion/Foundation/Log.h>
#include <Fluxion/Foundation/Memory/Allocator.h>

#include <string.h>

#define FLUXION_ASSET_IMPORT_LOG_CATEGORY "AssetImport"

// Where a cooked form starts out, before it is grown to fit.
#define FLUXION_ASSET_IMPORT_INITIAL_COOKED_BYTES (64u * 1024u)

// Past this, something is wrong with the importer rather than with the
// size of the asset.
#define FLUXION_ASSET_IMPORT_MAX_COOKED_BYTES (512u * 1024u * 1024u)

#define FLUXION_ASSET_IMPORT_MAX_TEXT (FLUXION_VFS_MAX_PATH + 1)

typedef struct FluxionAssetImportEntry
{
    FluxionUUID asset;
    FluxionAssetTypeId type;

    char name[FLUXION_ASSET_MAX_NAME_LENGTH + 1];
    char sourcePath[FLUXION_ASSET_IMPORT_MAX_TEXT];
    char cookedPath[FLUXION_ASSET_IMPORT_MAX_TEXT];
    char cookTarget[FLUXION_ASSET_COOK_TARGET_NAME_LENGTH + 1];

    u8 settings[FLUXION_ASSET_MAX_IMPORT_SETTINGS_BYTES];
    u32 settingsSize;
    u64 settingsHash;

    // Captured when the work is handed over, not looked up while it runs.
    //
    // A worker consulting the type registry would be reading a table the
    // owning thread is free to change -- and a type unregistered
    // mid-import would take its own import function with it.
    FluxionAssetImportFn import;
    void* importUserData;

    usize sourceBytes;

    // Written by a worker, read by the owning thread. The only field
    // where the two meet, which is why it is the only atomic one.
    FluxionAtomicI32 outcome;

    // Set by the owning thread once this entry's result has been put into
    // the database, so a second pump does not add it twice.
    bool applied;
} FluxionAssetImportEntry;

struct FluxionAssetImport
{
    FluxionAllocator* allocator;

    FluxionAssetImportEntry* entries;
    u32 entryCount;

    bool force;

    usize totalBytes;
    FluxionAtomicI64 completedBytes;
    FluxionAtomicI32 completedCount;
    FluxionAtomicI32 failedCount;
    FluxionAtomicI32 currentItem;
    FluxionAtomicI32 cancelled;

    FluxionJobHandle job;
};

// ---------------------------------------------------------------------
// The half that runs on a worker.
// ---------------------------------------------------------------------

static void Fluxion_AssetImport_Finish(FluxionAssetImport* import, FluxionAssetImportEntry* entry,
                                       FluxionAssetImportOutcome outcome, usize weight)
{
    Fluxion_AtomicI32_Store(&entry->outcome, (i32)outcome);

    // Added with a compare-and-exchange rather than a load followed by a
    // store. Several workers finish at once, and a read-modify-write
    // split into two steps loses whichever increments land between them:
    // the bar would simply stop short of the end, on some machines, some
    // of the time.
    for (;;)
    {
        i64 seen = Fluxion_AtomicI64_Load(&import->completedBytes);
        if (Fluxion_AtomicI64_CompareExchange(&import->completedBytes, &seen, seen + (i64)weight)) break;
    }

    Fluxion_AtomicI32_Increment(&import->completedCount);

    if (outcome >= FLUXION_ASSET_IMPORT_NO_IMPORTER) Fluxion_AtomicI32_Increment(&import->failedCount);
}

// Whether this entry's cooked form is already what these bytes and these
// settings would produce.
//
// Compared against the database, which only the owning thread writes and
// which is not being written while an import runs -- reading it here is
// therefore safe, and is what makes the ordinary case (nothing changed)
// cost one lookup instead of one cook.
static bool Fluxion_AssetImport_IsUpToDate(const FluxionAssetImportEntry* entry)
{
    if (Fluxion_UUID_IsNil(entry->asset)) return false;

    const FluxionAssetRecord* record = Fluxion_AssetDatabase_Find(entry->asset);
    if (record == NULL) return false;
    if (record->importSettingsHash != entry->settingsHash) return false;

    const char* cooked = Fluxion_AssetDatabase_GetCookedPathForTarget(record, entry->cookTarget);
    if (cooked[0] == '\0') return false;

    return Fluxion_Vfs_Exists(cooked);
}

// `data` is the import itself, not a pointer to a variable holding it.
//
// ParallelFor passes this pointer straight through and copies nothing --
// unlike a submitted job description, whose captured bytes ARE copied
// into the pool. Handing it the address of a local here would leave the
// workers reading a stack frame that had already gone.
static void Fluxion_AssetImport_RunOne(void* data, u32 index)
{
    FluxionAssetImport* import = (FluxionAssetImport*)data;
    FluxionAssetImportEntry* entry = &import->entries[index];

    if (Fluxion_AtomicI32_Load(&import->cancelled) != 0)
    {
        Fluxion_AssetImport_Finish(import, entry, FLUXION_ASSET_IMPORT_CANCELLED, entry->sourceBytes);
        return;
    }

    // Said before the work rather than after: what a progress display
    // wants to show is what is being worked on now.
    Fluxion_AtomicI32_Store(&import->currentItem, (i32)index);

    if (entry->import == NULL)
    {
        Fluxion_AssetImport_Finish(import, entry, FLUXION_ASSET_IMPORT_NO_IMPORTER, entry->sourceBytes);
        return;
    }

    if (!import->force && Fluxion_AssetImport_IsUpToDate(entry))
    {
        Fluxion_AssetImport_Finish(import, entry, FLUXION_ASSET_IMPORT_UP_TO_DATE, entry->sourceBytes);
        return;
    }

    usize sourceSize = 0;
    u8* sourceBytes = Fluxion_Vfs_ReadAll(entry->sourcePath, &sourceSize);
    if (sourceBytes == NULL)
    {
        Fluxion_AssetImport_Finish(import, entry, FLUXION_ASSET_IMPORT_SOURCE_MISSING, entry->sourceBytes);
        return;
    }

    FluxionAssetImportOutcome outcome = FLUXION_ASSET_IMPORT_COOK_FAILED;
    usize capacity = FLUXION_ASSET_IMPORT_INITIAL_COOKED_BYTES;

    for (;;)
    {
        u8* cooked = (u8*)Fluxion_Allocator_Alloc(import->allocator, capacity, FLUXION_DEFAULT_ALIGNMENT);
        if (cooked == NULL) break;

        FluxionStream stream;
        Fluxion_MemoryStream_InitWriter(&stream, cooked, capacity);

        const bool imported = entry->import(sourceBytes, sourceSize, &stream, entry->importUserData);
        const bool overflowed = Fluxion_Stream_HasOverflowed(&stream);

        if (imported && !overflowed)
        {
            outcome = Fluxion_Vfs_WriteAll(entry->cookedPath, cooked, Fluxion_Stream_GetPosition(&stream))
                          ? FLUXION_ASSET_IMPORT_COOKED
                          : FLUXION_ASSET_IMPORT_WRITE_FAILED;
            Fluxion_Allocator_Free(import->allocator, cooked, capacity);
            break;
        }

        Fluxion_Allocator_Free(import->allocator, cooked, capacity);

        // An importer that ran out of room gets more; one that failed for
        // its own reasons does not, because trying again with a bigger
        // buffer would fail the same way, more slowly.
        if (!overflowed) break;

        capacity *= 2u;
        if (capacity > FLUXION_ASSET_IMPORT_MAX_COOKED_BYTES) break;
    }

    Fluxion_Vfs_FreeBuffer(sourceBytes, sourceSize);
    Fluxion_AssetImport_Finish(import, entry, outcome, entry->sourceBytes);
}

// ---------------------------------------------------------------------
// Starting and stopping.
// ---------------------------------------------------------------------

static void Fluxion_AssetImport_CopyBounded(char* destination, usize capacity, const char* source)
{
    destination[0] = '\0';
    if (source == NULL) return;

    const usize length = strlen(source);
    const usize copied = length < capacity ? length : capacity - 1;
    memcpy(destination, source, copied);
    destination[copied] = '\0';
}

FluxionAssetImport* Fluxion_AssetImport_Begin(const FluxionAssetImportDesc* desc)
{
    if (!Fluxion_AssetDatabase_IsInitialized() || !Fluxion_AssetTypes_IsInitialized()) return NULL;
    if (desc == NULL || desc->items == NULL || desc->itemCount == 0) return NULL;

    FluxionAllocator* allocator = Fluxion_Vfs_GetAllocator();
    if (allocator == NULL) allocator = Fluxion_DefaultAllocator();

    FluxionAssetImport* import = (FluxionAssetImport*)Fluxion_Allocator_Alloc(allocator, sizeof(FluxionAssetImport), FLUXION_DEFAULT_ALIGNMENT);
    if (import == NULL) return NULL;

    memset(import, 0, sizeof(*import));
    import->allocator = allocator;
    import->entryCount = desc->itemCount;
    import->force = desc->force;
    Fluxion_AtomicI32_Store(&import->currentItem, -1);

    import->entries = (FluxionAssetImportEntry*)Fluxion_Allocator_Alloc(allocator, (usize)desc->itemCount * sizeof(FluxionAssetImportEntry),
                                                                        FLUXION_DEFAULT_ALIGNMENT);
    if (import->entries == NULL)
    {
        Fluxion_Allocator_Free(allocator, import, sizeof(FluxionAssetImport));
        return NULL;
    }

    memset(import->entries, 0, (usize)desc->itemCount * sizeof(FluxionAssetImportEntry));

    for (u32 i = 0; i < desc->itemCount; ++i)
    {
        const FluxionAssetImportItem* item = &desc->items[i];
        FluxionAssetImportEntry* entry = &import->entries[i];

        entry->asset = item->asset;
        entry->type = item->type;

        Fluxion_AssetImport_CopyBounded(entry->name, sizeof(entry->name), item->name);
        Fluxion_AssetImport_CopyBounded(entry->sourcePath, sizeof(entry->sourcePath), item->sourcePath);
        Fluxion_AssetImport_CopyBounded(entry->cookedPath, sizeof(entry->cookedPath), item->cookedPath);
        Fluxion_AssetImport_CopyBounded(entry->cookTarget, sizeof(entry->cookTarget), item->cookTarget);

        if (item->importSettings != NULL && item->importSettingsSize > 0 &&
            item->importSettingsSize <= FLUXION_ASSET_MAX_IMPORT_SETTINGS_BYTES)
        {
            memcpy(entry->settings, item->importSettings, item->importSettingsSize);
            entry->settingsSize = item->importSettingsSize;
            entry->settingsHash = Fluxion_HashBytes64(entry->settings, entry->settingsSize);
        }

        // The type is looked up HERE, on the thread that owns the
        // registry, and what is kept is the function rather than the way
        // to find it again.
        const FluxionAssetTypeDesc* type = Fluxion_AssetTypes_Find(item->type);
        if (type != NULL)
        {
            entry->import = type->import;
            entry->importUserData = type->userData;
        }

        // Weighed by how large the source is, so the bar moves at
        // something like the rate the work does. One is added to every
        // item so that a list of empty files still finishes at one rather
        // than dividing by zero.
        entry->sourceBytes = Fluxion_Vfs_GetSize(entry->sourcePath) + 1u;
        import->totalBytes += entry->sourceBytes;

        Fluxion_AtomicI32_Store(&entry->outcome, (i32)FLUXION_ASSET_IMPORT_PENDING);
    }

    import->job = (FluxionJobHandle){ FLUXION_HANDLE_INVALID_INDEX, 0 };

    if (Fluxion_JobSystem_IsInitialized())
    {
        // One item per job rather than a batch: the items differ wildly
        // in how long they take, and a batch is only as quick as the
        // slowest thing in it.
        import->job = Fluxion_JobSystem_ParallelFor(import->entryCount, 1, Fluxion_AssetImport_RunOne, import, NULL, 0);
    }

    if (!FLUXION_HANDLE_IS_VALID(import->job))
    {
        // Nothing to hand the work to. Done here instead, so a caller
        // that never started a job system still gets its assets imported.
        for (u32 i = 0; i < import->entryCount; ++i) Fluxion_AssetImport_RunOne(import, i);
    }

    return import;
}

bool Fluxion_AssetImport_IsFinished(const FluxionAssetImport* import)
{
    if (import == NULL) return true;
    return (u32)Fluxion_AtomicI32_Load(&import->completedCount) >= import->entryCount;
}

FluxionAssetImportProgress Fluxion_AssetImport_GetProgress(const FluxionAssetImport* import)
{
    FluxionAssetImportProgress progress;
    memset(&progress, 0, sizeof(progress));
    progress.currentItem = -1;

    if (import == NULL) return progress;

    progress.total = import->entryCount;
    progress.completed = (u32)Fluxion_AtomicI32_Load(&import->completedCount);
    progress.failed = (u32)Fluxion_AtomicI32_Load(&import->failedCount);
    progress.currentItem = Fluxion_AtomicI32_Load(&import->currentItem);

    const i64 done = Fluxion_AtomicI64_Load(&import->completedBytes);
    progress.fraction = (import->totalBytes > 0) ? (f32)((f64)done / (f64)import->totalBytes) : 1.0f;

    // Clamped, because the two counters are read one after the other and
    // a worker may finish between the two reads. A bar that briefly shows
    // more than everything looks like a bug in the renderer.
    if (progress.fraction > 1.0f) progress.fraction = 1.0f;

    return progress;
}

void Fluxion_AssetImport_Cancel(FluxionAssetImport* import)
{
    if (import == NULL) return;
    Fluxion_AtomicI32_Store(&import->cancelled, 1);
}

u32 Fluxion_AssetImport_Update(FluxionAssetImport* import)
{
    if (import == NULL) return 0;

    u32 applied = 0;

    for (u32 i = 0; i < import->entryCount; ++i)
    {
        FluxionAssetImportEntry* entry = &import->entries[i];
        if (entry->applied) continue;

        const FluxionAssetImportOutcome outcome = (FluxionAssetImportOutcome)Fluxion_AtomicI32_Load(&entry->outcome);
        if (outcome == FLUXION_ASSET_IMPORT_PENDING) continue;

        entry->applied = true;

        // Only a fresh cook changes what the database holds. Anything
        // else -- already up to date, cancelled, failed -- leaves it
        // exactly as it was, which is the point of reporting per asset
        // rather than giving up on the batch.
        if (outcome != FLUXION_ASSET_IMPORT_COOKED) continue;

        FluxionAssetCookedForm form;
        memset(&form, 0, sizeof(form));
        Fluxion_AssetImport_CopyBounded(form.target, sizeof(form.target), entry->cookTarget);
        form.path = entry->cookedPath;

        FluxionAssetDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.id = entry->asset;
        desc.type = entry->type;
        desc.name = entry->name;
        desc.sourcePath = entry->sourcePath;
        desc.cookedForms = &form;
        desc.cookedFormCount = 1;
        desc.importSettings = entry->settingsSize > 0 ? entry->settings : NULL;
        desc.importSettingsSize = entry->settingsSize;

        // Re-imported rather than added twice. The record is replaced
        // whole: what a cook produces is the complete answer for this
        // asset, and merging it with what was there would mean keeping
        // parts of an older answer nobody asked for.
        if (!Fluxion_UUID_IsNil(entry->asset)) Fluxion_AssetDatabase_Remove(entry->asset);

        FluxionUUID assigned;
        if (Fluxion_AssetDatabase_Add(&desc, &assigned))
        {
            entry->asset = assigned;
            ++applied;
        }
        else
        {
            FLUXION_LOG_ERROR(FLUXION_ASSET_IMPORT_LOG_CATEGORY, "'%s' was cooked but could not be recorded", entry->name);
            Fluxion_AtomicI32_Store(&entry->outcome, (i32)FLUXION_ASSET_IMPORT_WRITE_FAILED);
            Fluxion_AtomicI32_Increment(&import->failedCount);
        }
    }

    return applied;
}

u32 Fluxion_AssetImport_GetItemCount(const FluxionAssetImport* import)
{
    return import ? import->entryCount : 0u;
}

FluxionAssetImportOutcome Fluxion_AssetImport_GetOutcomeAt(const FluxionAssetImport* import, u32 index)
{
    if (import == NULL || index >= import->entryCount) return FLUXION_ASSET_IMPORT_PENDING;
    return (FluxionAssetImportOutcome)Fluxion_AtomicI32_Load(&import->entries[index].outcome);
}

const char* Fluxion_AssetImport_GetNameAt(const FluxionAssetImport* import, u32 index)
{
    if (import == NULL || index >= import->entryCount) return "";
    return import->entries[index].name;
}

FluxionUUID Fluxion_AssetImport_GetAssetAt(const FluxionAssetImport* import, u32 index)
{
    FluxionUUID nil;
    memset(&nil, 0, sizeof(nil));

    if (import == NULL || index >= import->entryCount) return nil;
    return import->entries[index].asset;
}

void Fluxion_AssetImport_End(FluxionAssetImport* import)
{
    if (import == NULL) return;

    if (FLUXION_HANDLE_IS_VALID(import->job)) Fluxion_JobSystem_Wait(import->job);

    Fluxion_AssetImport_Update(import);

    Fluxion_Allocator_Free(import->allocator, import->entries, (usize)import->entryCount * sizeof(FluxionAssetImportEntry));
    Fluxion_Allocator_Free(import->allocator, import, sizeof(FluxionAssetImport));
}
