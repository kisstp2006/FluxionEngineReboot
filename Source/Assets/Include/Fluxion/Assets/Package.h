#pragma once

#include <Fluxion/Assets/AssetDatabase.h>
#include <Fluxion/Assets/AssetType.h>
#include <Fluxion/Assets/VirtualFileSystem.h>
#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Foundation/UUID.h>

#ifdef __cplusplus
extern "C" {
#endif

// One file holding everything a built game reads, and the rules that
// decide what goes into it.
//
// A package is an ARCHIVE, nothing cleverer: paths in, bytes out. That is
// on purpose. It means a built game mounts a package where a project
// being worked on mounts a folder, and the code that loads an asset
// cannot tell the difference -- so there is no second loading path that
// only runs after shipping and is therefore only tested after shipping.
//
// The index a game needs in order to know what its assets ARE goes in as
// one more entry, at the path below.

#define FLUXION_PACKAGE_FORMAT_VERSION 1
#define FLUXION_PACKAGE_FILE_MAGIC     0x464C5850u // "FLXP"

// Where the asset index sits inside a package.
#define FLUXION_PACKAGE_DATABASE_PATH "AssetDatabase.fluxdb"

// Reading a package.
//
// The whole index is read when this is created; the bytes of an entry are
// read when someone asks for them, with the file opened again for each
// read. That keeps a package of any size out of memory and keeps reads
// safe to make from several threads at once -- which is exactly what
// loading assets does.
//
// NULL when the file is missing, is not a package, was written by a newer
// build, or has an index that does not add up.
FluxionVfsSource* Fluxion_VfsPackageSource_CreateFromFile(const char* realPath);

// ---------------------------------------------------------------------
// Building one.
// ---------------------------------------------------------------------

// What a build says about a type, overriding what the type says about
// itself. Named by type name rather than by id because this is written in
// a build setting, by hand, and read by a person.
typedef struct FluxionAssetShipOverride
{
    char typeName[FLUXION_ASSET_MAX_TYPE_NAME_LENGTH + 1];
    FluxionAssetShipPolicy policy;
} FluxionAssetShipOverride;

typedef struct FluxionPackageBuildDesc
{
    const FluxionAssetShipOverride* overrides;
    u32 overrideCount;

    // Which mount everything shipped must live under. Empty means
    // "assets". An asset whose bytes are somewhere else is an error and
    // not a silent omission -- there is no second scheme in a built game
    // for them to have gone to.
    char scheme[FLUXION_VFS_MAX_SCHEME_LENGTH + 1];

    // Which cooked form to take, for the assets that have more than one.
    //
    // Empty asks for the one that suits every build, which is what most
    // assets have and all of them had before compressed textures existed.
    // An asset with no entry for this target falls back to that one, so a
    // mesh does not have to be listed under every target a project ships
    // to.
    char cookTarget[FLUXION_ASSET_COOK_TARGET_NAME_LENGTH + 1];
} FluxionPackageBuildDesc;

typedef enum FluxionPackageOutcome
{
    FLUXION_PACKAGE_OUTCOME_INCLUDED_COOKED = 0,
    FLUXION_PACKAGE_OUTCOME_INCLUDED_SOURCE,

    // Left out because that is what was asked for. Not an error.
    FLUXION_PACKAGE_OUTCOME_EXCLUDED,

    // Errors from here down. Each one is a game that would have started
    // and then not worked, caught at the only moment when it can still be
    // fixed.
    FLUXION_PACKAGE_OUTCOME_UNKNOWN_TYPE,
    FLUXION_PACKAGE_OUTCOME_MISSING_DATA,
    FLUXION_PACKAGE_OUTCOME_WRONG_SCHEME,
    FLUXION_PACKAGE_OUTCOME_BROKEN_DEPENDENCY,
} FluxionPackageOutcome;

typedef struct FluxionPackageBuildEntry
{
    FluxionUUID asset;
    char name[FLUXION_ASSET_MAX_NAME_LENGTH + 1];
    char typeName[FLUXION_ASSET_MAX_TYPE_NAME_LENGTH + 1];
    FluxionPackageOutcome outcome;

    // For a broken dependency: which asset was needed and is not there.
    // Nil otherwise.
    FluxionUUID culprit;
} FluxionPackageBuildEntry;

// What went in, what stayed out, and why.
//
// Without this, "did the source really stay out?" can only be answered by
// looking at the bytes of the finished package -- which means in practice
// it does not get answered.
typedef struct FluxionPackageBuildReport
{
    FluxionPackageBuildEntry* entries;
    u32 entryCount;

    u32 includedCount;
    u32 excludedCount;
    u32 errorCount;
} FluxionPackageBuildReport;

// Writes a package containing every asset the rules say should ship.
//
// Nothing is written when anything is wrong: a package missing one of the
// things it claims to hold is worse than no package. The report is filled
// in either way, which is the point of separating the two.
//
// `outReport` may be NULL, and then the only answer is the return value.
// Free a filled report with Fluxion_Package_FreeReport.
bool Fluxion_Package_Build(const FluxionPackageBuildDesc* desc, const char* outputRealPath, FluxionPackageBuildReport* outReport);

void Fluxion_Package_FreeReport(FluxionPackageBuildReport* report);

// Which way a type would go under these rules. Exposed so a build tool
// can show the decision before making it, and so it can be tested
// directly rather than only through the bytes it produces.
FluxionAssetShipPolicy Fluxion_Package_ResolveShipPolicy(const FluxionPackageBuildDesc* desc, const FluxionAssetTypeDesc* type);

#ifdef __cplusplus
}
#endif
