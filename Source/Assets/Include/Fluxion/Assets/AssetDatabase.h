#pragma once

#include <Fluxion/Assets/AssetTypeId.h>
#include <Fluxion/Assets/VirtualFileSystem.h>
#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Serialization/Stream.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Foundation/UUID.h>

#ifdef __cplusplus
extern "C" {
#endif

// What the project knows about each asset: which one it is, what kind it
// is, where its source is, where its cooked form is, and what it needs in
// order to be usable.
//
// In a project being worked on, this is built by looking at what is
// there. In a shipped game it is the package's own index, read once --
// and the source paths in it are empty, because nothing in a running game
// has any use for them.

// What this build writes and the newest it will read. A file from a newer
// build is refused rather than half understood.
#define FLUXION_ASSET_DATABASE_VERSION 1

// A path in this database is a path in the file system above, so it is
// bounded by the same thing rather than by a second number that could
// disagree with it. A name is only ever read by a person.
#define FLUXION_ASSET_MAX_PATH_LENGTH (FLUXION_VFS_MAX_PATH - 1)
#define FLUXION_ASSET_MAX_NAME_LENGTH 127

// Text and dependency lists both live in shared pools rather than in the
// record. This is the one table that grows with the size of a project, so
// a per-record path buffer would be paid for by every asset whether it
// had a path or not -- unlike the small fixed-size descriptors elsewhere
// in the engine, where the generous cap really is cheaper than the
// bookkeeping.
typedef struct FluxionAssetRecord
{
    FluxionUUID id;
    FluxionAssetTypeId type;
    u32 version;

    // The database's own bookkeeping. Read them through the accessors
    // below rather than directly.
    //
    // Zero is a real offset here and means the empty string, because the
    // text pool begins with a terminator that nothing else uses. "No
    // path" and "an empty path" are therefore the same value, which is
    // exactly right -- and it means no separate way of saying "none" can
    // be forgotten at one of the places that sets these.
    u32 nameOffset;
    u32 sourcePathOffset;
    u32 cookedPathOffset;
    u32 dependencyOffset;
    u32 dependencyCount;
} FluxionAssetRecord;

typedef struct FluxionAssetDesc
{
    // Nil asks for a fresh one, which is what importing something new
    // wants; a set value is used as given, which is what reading a
    // database back wants.
    FluxionUUID id;

    FluxionAssetTypeId type;

    // All optional. A name is for reading logs and build reports -- it
    // resolves nothing, and two assets may share one.
    const char* name;
    const char* sourcePath;
    const char* cookedPath;

    u32 version;

    const FluxionUUID* dependencies;
    u32 dependencyCount;
} FluxionAssetDesc;

void Fluxion_AssetDatabase_Init(FluxionAllocator* allocator);
void Fluxion_AssetDatabase_Shutdown(void);
bool Fluxion_AssetDatabase_IsInitialized(void);

bool Fluxion_AssetDatabase_Add(const FluxionAssetDesc* desc, FluxionUUID* outId);

// Removing leaves the entry's text and dependency list behind in the
// pools until the whole database is cleared. Bounded by how much one
// session removes, and worth it for a table this size.
bool Fluxion_AssetDatabase_Remove(FluxionUUID id);
void Fluxion_AssetDatabase_Clear(void);

// Every returned record pointer, and every string returned from one, is
// good only until the next Add, Remove or Clear. The records live in one
// growable run, so adding may move all of them at once. Hold the id, not
// the pointer.
const FluxionAssetRecord* Fluxion_AssetDatabase_Find(FluxionUUID id);

u32 Fluxion_AssetDatabase_GetCount(void);

// Index order is not stable across removals -- an entry is removed by
// moving the last one into its place. Enumerate for a pass over
// everything, look up by id for anything that must stay pointing at the
// same asset.
const FluxionAssetRecord* Fluxion_AssetDatabase_GetAt(u32 index);

const char* Fluxion_AssetDatabase_GetName(const FluxionAssetRecord* record);
const char* Fluxion_AssetDatabase_GetSourcePath(const FluxionAssetRecord* record);
const char* Fluxion_AssetDatabase_GetCookedPath(const FluxionAssetRecord* record);
const FluxionUUID* Fluxion_AssetDatabase_GetDependencies(const FluxionAssetRecord* record, u32* outCount);

// Reads or writes the whole database, depending on the stream's mode.
// Reading clears whatever was there first -- this is a load, not a merge,
// for the same reason loading a scene is.
bool Fluxion_AssetDatabase_Serialize(FluxionStream* stream);

// What a build writes instead of the whole thing: fewer entries, and
// different paths in them.
//
// This exists so that the index a game ships and the database an editor
// keeps go through ONE writer. The alternative -- a second writer that
// knows the same format -- is two things that have to agree forever, and
// the day they stop agreeing is the day a shipped game cannot read its
// own index.
typedef struct FluxionAssetDatabaseWriteFilter
{
    // Which records to write at all. NULL writes every one.
    bool (*shouldWrite)(const FluxionAssetRecord* record, void* userData);

    // Where the bytes actually ended up. NULL writes what is stored.
    const char* (*cookedPathFor)(const FluxionAssetRecord* record, void* userData);

    // A shipped index sets this false, and the source path in it comes
    // out empty. That is not tidiness: it is the reason a running game
    // has nothing to reach for even if some code one day tried.
    bool includeSourcePaths;

    void* userData;
} FluxionAssetDatabaseWriteFilter;

// Write only. Reading uses Serialize above -- a filter on the way in
// would mean a file whose contents depend on who opened it.
bool Fluxion_AssetDatabase_SerializeFiltered(FluxionStream* stream, const FluxionAssetDatabaseWriteFilter* filter);

#ifdef __cplusplus
}
#endif
