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

#include <Fluxion/Assets/AssetDatabase.h>

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Containers/DynamicArray.h>
#include <Fluxion/Foundation/Containers/HashMap.h>
#include <Fluxion/Foundation/Hashing.h>
#include <Fluxion/Foundation/Log.h>

#include <string.h>

#define FLUXION_ASSET_DATABASE_LOG_CATEGORY "AssetDatabase"

// One cooked form as the database keeps it: the target it is for, and
// where its path sits in the text pool.
typedef struct FluxionAssetCookedFormEntry
{
    char target[FLUXION_ASSET_COOK_TARGET_NAME_LENGTH + 1];
    u32 pathOffset;
} FluxionAssetCookedFormEntry;

static FluxionAllocator* s_allocator = NULL;
static FluxionDynamicArray s_records;        // FluxionAssetRecord
static FluxionDynamicArray s_text;           // char
static FluxionDynamicArray s_dependencies;   // FluxionUUID
static FluxionDynamicArray s_cookedForms;    // FluxionAssetCookedFormEntry
static FluxionDynamicArray s_importSettings; // u8
static FluxionHashMap s_byId;                // FluxionUUID -> u32 record index
static bool s_initialized = false;

// The text pool always begins with a lone terminator, so offset zero is
// the empty string. That is what makes "this asset has no source path"
// expressible without a separate sentinel that one of the writers could
// forget to use.
static void Fluxion_AssetDatabase_ResetPools(void)
{
    Fluxion_DynamicArray_Clear(&s_records);
    Fluxion_DynamicArray_Clear(&s_text);
    Fluxion_DynamicArray_Clear(&s_dependencies);
    Fluxion_DynamicArray_Clear(&s_cookedForms);
    Fluxion_DynamicArray_Clear(&s_importSettings);

    const char terminator = '\0';
    Fluxion_DynamicArray_Push(&s_text, &terminator);
}

void Fluxion_AssetDatabase_Init(FluxionAllocator* allocator)
{
    FLUXION_ASSERT_MSG(!s_initialized, "Fluxion_AssetDatabase_Init called twice without a Shutdown in between");

    s_allocator = allocator ? allocator : Fluxion_DefaultAllocator();
    Fluxion_DynamicArray_Init(&s_records, s_allocator, sizeof(FluxionAssetRecord));
    Fluxion_DynamicArray_Init(&s_text, s_allocator, sizeof(char));
    Fluxion_DynamicArray_Init(&s_dependencies, s_allocator, sizeof(FluxionUUID));
    Fluxion_DynamicArray_Init(&s_cookedForms, s_allocator, sizeof(FluxionAssetCookedFormEntry));
    Fluxion_DynamicArray_Init(&s_importSettings, s_allocator, sizeof(u8));
    Fluxion_HashMap_Init(&s_byId, s_allocator, sizeof(FluxionUUID), sizeof(u32), Fluxion_HashBytes64, Fluxion_BytesEqual);

    s_initialized = true;
    Fluxion_AssetDatabase_ResetPools();
}

void Fluxion_AssetDatabase_Shutdown(void)
{
    if (!s_initialized) return;

    Fluxion_HashMap_Destroy(&s_byId);
    Fluxion_DynamicArray_Destroy(&s_importSettings);
    Fluxion_DynamicArray_Destroy(&s_cookedForms);
    Fluxion_DynamicArray_Destroy(&s_dependencies);
    Fluxion_DynamicArray_Destroy(&s_text);
    Fluxion_DynamicArray_Destroy(&s_records);
    s_allocator = NULL;
    s_initialized = false;
}

bool Fluxion_AssetDatabase_IsInitialized(void)
{
    return s_initialized;
}

void Fluxion_AssetDatabase_Clear(void)
{
    if (!s_initialized) return;

    Fluxion_HashMap_Destroy(&s_byId);
    Fluxion_HashMap_Init(&s_byId, s_allocator, sizeof(FluxionUUID), sizeof(u32), Fluxion_HashBytes64, Fluxion_BytesEqual);
    Fluxion_AssetDatabase_ResetPools();
}

static u32 Fluxion_AssetDatabase_PushText(const char* text)
{
    if (!text || text[0] == '\0') return 0;

    const u32 offset = (u32)s_text.count;
    const usize length = strlen(text);
    for (usize i = 0; i <= length; ++i) Fluxion_DynamicArray_Push(&s_text, &text[i]);
    return offset;
}

static const char* Fluxion_AssetDatabase_TextAt(u32 offset)
{
    if (!s_initialized || offset >= s_text.count) return "";
    return (const char*)Fluxion_DynamicArray_At(&s_text, offset);
}

static bool Fluxion_AssetDatabase_CheckLength(const char* text, usize limit, const char* what)
{
    if (!text) return true;
    if (strlen(text) <= limit) return true;
    FLUXION_LOG_ERROR(FLUXION_ASSET_DATABASE_LOG_CATEGORY, "%s is longer than this database can hold", what);
    return false;
}

// Copies one asset's cooked forms into the pool, from whichever of the
// two ways the caller used to say where its bytes are.
static bool Fluxion_AssetDatabase_PushCookedForms(const FluxionAssetDesc* desc, u32* outOffset, u32* outCount)
{
    *outOffset = (u32)s_cookedForms.count;
    *outCount = 0;

    if (desc->cookedPath != NULL && desc->cookedPath[0] != '\0')
    {
        FluxionAssetCookedFormEntry entry;
        memset(&entry, 0, sizeof(entry));
        entry.pathOffset = Fluxion_AssetDatabase_PushText(desc->cookedPath);
        Fluxion_DynamicArray_Push(&s_cookedForms, &entry);
        *outCount = 1;
        return true;
    }

    if (desc->cookedForms == NULL || desc->cookedFormCount == 0) return true;

    if (desc->cookedFormCount > FLUXION_ASSET_MAX_COOKED_FORMS)
    {
        FLUXION_LOG_ERROR(FLUXION_ASSET_DATABASE_LOG_CATEGORY, "an asset has more cooked forms than this database holds");
        return false;
    }

    for (u32 i = 0; i < desc->cookedFormCount; ++i)
    {
        const FluxionAssetCookedForm* form = &desc->cookedForms[i];

        if (!Fluxion_AssetDatabase_CheckLength(form->path, FLUXION_ASSET_MAX_PATH_LENGTH, "an asset cooked path")) return false;

        // Two entries for one target would make which one a build takes
        // depend on the order they happened to be written in.
        for (u32 j = 0; j < i; ++j)
        {
            if (strcmp(desc->cookedForms[j].target, form->target) != 0) continue;
            FLUXION_LOG_ERROR(FLUXION_ASSET_DATABASE_LOG_CATEGORY, "an asset names the cook target '%s' twice", form->target);
            return false;
        }

        const usize targetLength = strlen(form->target);
        if (targetLength > FLUXION_ASSET_COOK_TARGET_NAME_LENGTH)
        {
            FLUXION_LOG_ERROR(FLUXION_ASSET_DATABASE_LOG_CATEGORY, "a cook target name is longer than this database holds");
            return false;
        }

        FluxionAssetCookedFormEntry entry;
        memset(&entry, 0, sizeof(entry));
        memcpy(entry.target, form->target, targetLength);
        entry.pathOffset = Fluxion_AssetDatabase_PushText(form->path);

        Fluxion_DynamicArray_Push(&s_cookedForms, &entry);
        ++(*outCount);
    }

    return true;
}

bool Fluxion_AssetDatabase_Add(const FluxionAssetDesc* desc, FluxionUUID* outId)
{
    if (!s_initialized || !desc) return false;

    if (!Fluxion_AssetDatabase_CheckLength(desc->name, FLUXION_ASSET_MAX_NAME_LENGTH, "an asset name")) return false;
    if (!Fluxion_AssetDatabase_CheckLength(desc->sourcePath, FLUXION_ASSET_MAX_PATH_LENGTH, "an asset source path")) return false;
    if (!Fluxion_AssetDatabase_CheckLength(desc->cookedPath, FLUXION_ASSET_MAX_PATH_LENGTH, "an asset cooked path")) return false;

    // Two ways of saying where the bytes are is two things that can
    // disagree, so saying it twice is refused rather than resolved.
    if (desc->cookedPath != NULL && desc->cookedPath[0] != '\0' && desc->cookedFormCount > 0)
    {
        FLUXION_LOG_ERROR(FLUXION_ASSET_DATABASE_LOG_CATEGORY, "an asset gives both a cooked path and a list of cooked forms");
        return false;
    }

    if (desc->importSettingsSize > FLUXION_ASSET_MAX_IMPORT_SETTINGS_BYTES)
    {
        FLUXION_LOG_ERROR(FLUXION_ASSET_DATABASE_LOG_CATEGORY, "an asset's import settings are larger than this database holds");
        return false;
    }

    const FluxionUUID id = Fluxion_UUID_IsNil(desc->id) ? Fluxion_UUID_Generate() : desc->id;

    if (Fluxion_HashMap_Find(&s_byId, &id))
    {
        FLUXION_LOG_ERROR(FLUXION_ASSET_DATABASE_LOG_CATEGORY, "an asset with this id is already in the database");
        return false;
    }

    FluxionAssetRecord record;
    memset(&record, 0, sizeof(record));
    record.id = id;
    record.type = desc->type;
    record.version = desc->version;
    record.nameOffset = Fluxion_AssetDatabase_PushText(desc->name);
    record.sourcePathOffset = Fluxion_AssetDatabase_PushText(desc->sourcePath);

    if (!Fluxion_AssetDatabase_PushCookedForms(desc, &record.cookedOffset, &record.cookedCount)) return false;

    record.importSettingsOffset = (u32)s_importSettings.count;
    record.importSettingsSize = (desc->importSettings != NULL) ? desc->importSettingsSize : 0u;
    record.importSettingsHash = 0;
    if (record.importSettingsSize > 0)
    {
        const u8* bytes = (const u8*)desc->importSettings;
        for (u32 i = 0; i < record.importSettingsSize; ++i) Fluxion_DynamicArray_Push(&s_importSettings, &bytes[i]);
        record.importSettingsHash = Fluxion_HashBytes64(bytes, record.importSettingsSize);
    }

    record.dependencyOffset = (u32)s_dependencies.count;
    record.dependencyCount = desc->dependencies ? desc->dependencyCount : 0;
    for (u32 i = 0; i < record.dependencyCount; ++i) Fluxion_DynamicArray_Push(&s_dependencies, &desc->dependencies[i]);

    const u32 index = (u32)s_records.count;
    if (!Fluxion_DynamicArray_Push(&s_records, &record)) return false;
    if (!Fluxion_HashMap_Set(&s_byId, &id, &index))
    {
        Fluxion_DynamicArray_Pop(&s_records);
        return false;
    }

    if (outId) *outId = id;
    return true;
}

bool Fluxion_AssetDatabase_Remove(FluxionUUID id)
{
    if (!s_initialized) return false;

    const u32* found = (const u32*)Fluxion_HashMap_Find(&s_byId, &id);
    if (!found) return false;

    const u32 index = *found;
    const u32 lastIndex = (u32)s_records.count - 1;

    if (index != lastIndex)
    {
        FluxionAssetRecord* target = (FluxionAssetRecord*)Fluxion_DynamicArray_At(&s_records, index);
        const FluxionAssetRecord* last = (const FluxionAssetRecord*)Fluxion_DynamicArray_At(&s_records, lastIndex);
        *target = *last;

        // The record that moved is now somewhere else, and the map is the
        // only thing that knows where anything is. Leaving this out is the
        // kind of mistake a lookup of the removed asset would not notice.
        Fluxion_HashMap_Set(&s_byId, &target->id, &index);
    }

    Fluxion_DynamicArray_Pop(&s_records);
    Fluxion_HashMap_Remove(&s_byId, &id);
    return true;
}

const FluxionAssetRecord* Fluxion_AssetDatabase_Find(FluxionUUID id)
{
    if (!s_initialized) return NULL;

    const u32* found = (const u32*)Fluxion_HashMap_Find(&s_byId, &id);
    if (!found) return NULL;
    return (const FluxionAssetRecord*)Fluxion_DynamicArray_At(&s_records, *found);
}

u32 Fluxion_AssetDatabase_GetCount(void)
{
    return s_initialized ? (u32)s_records.count : 0;
}

const FluxionAssetRecord* Fluxion_AssetDatabase_GetAt(u32 index)
{
    if (!s_initialized || index >= s_records.count) return NULL;
    return (const FluxionAssetRecord*)Fluxion_DynamicArray_At(&s_records, index);
}

const char* Fluxion_AssetDatabase_GetName(const FluxionAssetRecord* record)
{
    return record ? Fluxion_AssetDatabase_TextAt(record->nameOffset) : "";
}

const char* Fluxion_AssetDatabase_GetSourcePath(const FluxionAssetRecord* record)
{
    return record ? Fluxion_AssetDatabase_TextAt(record->sourcePathOffset) : "";
}

static const FluxionAssetCookedFormEntry* Fluxion_AssetDatabase_CookedFormAt(const FluxionAssetRecord* record, u32 index)
{
    if (!s_initialized || !record || index >= record->cookedCount) return NULL;
    if (record->cookedOffset + index >= s_cookedForms.count) return NULL;
    return (const FluxionAssetCookedFormEntry*)Fluxion_DynamicArray_At(&s_cookedForms, record->cookedOffset + index);
}

u32 Fluxion_AssetDatabase_GetCookedFormCount(const FluxionAssetRecord* record)
{
    return record ? record->cookedCount : 0u;
}

const char* Fluxion_AssetDatabase_GetCookedFormTargetAt(const FluxionAssetRecord* record, u32 index)
{
    const FluxionAssetCookedFormEntry* entry = Fluxion_AssetDatabase_CookedFormAt(record, index);
    return entry ? entry->target : "";
}

const char* Fluxion_AssetDatabase_GetCookedFormPathAt(const FluxionAssetRecord* record, u32 index)
{
    const FluxionAssetCookedFormEntry* entry = Fluxion_AssetDatabase_CookedFormAt(record, index);
    return entry ? Fluxion_AssetDatabase_TextAt(entry->pathOffset) : "";
}

// The one that suits every build, or the first there is -- what a caller
// that does not care which build is asking for.
const char* Fluxion_AssetDatabase_GetCookedPath(const FluxionAssetRecord* record)
{
    if (!record || record->cookedCount == 0) return "";

    for (u32 i = 0; i < record->cookedCount; ++i)
    {
        const FluxionAssetCookedFormEntry* entry = Fluxion_AssetDatabase_CookedFormAt(record, i);
        if (entry && entry->target[0] == '\0') return Fluxion_AssetDatabase_TextAt(entry->pathOffset);
    }

    return Fluxion_AssetDatabase_GetCookedFormPathAt(record, 0);
}

const char* Fluxion_AssetDatabase_GetCookedPathForTarget(const FluxionAssetRecord* record, const char* target)
{
    if (!record) return "";
    if (target == NULL || target[0] == '\0') return Fluxion_AssetDatabase_GetCookedPath(record);

    for (u32 i = 0; i < record->cookedCount; ++i)
    {
        const FluxionAssetCookedFormEntry* entry = Fluxion_AssetDatabase_CookedFormAt(record, i);
        if (entry && strcmp(entry->target, target) == 0) return Fluxion_AssetDatabase_TextAt(entry->pathOffset);
    }

    // Falling back to the general one, so an asset cooked once for
    // everything need not list every target it does not care about.
    return Fluxion_AssetDatabase_GetCookedPath(record);
}

const void* Fluxion_AssetDatabase_GetImportSettings(const FluxionAssetRecord* record, u32* outSize)
{
    if (outSize) *outSize = 0;
    if (!s_initialized || !record || record->importSettingsSize == 0) return NULL;
    if (record->importSettingsOffset + record->importSettingsSize > s_importSettings.count) return NULL;

    if (outSize) *outSize = record->importSettingsSize;
    return Fluxion_DynamicArray_At(&s_importSettings, record->importSettingsOffset);
}

const FluxionUUID* Fluxion_AssetDatabase_GetDependencies(const FluxionAssetRecord* record, u32* outCount)
{
    if (outCount) *outCount = 0;
    if (!s_initialized || !record || record->dependencyCount == 0) return NULL;
    if (record->dependencyOffset + record->dependencyCount > s_dependencies.count) return NULL;

    if (outCount) *outCount = record->dependencyCount;
    return (const FluxionUUID*)Fluxion_DynamicArray_At(&s_dependencies, record->dependencyOffset);
}

// ---------------------------------------------------------------------
// Writing the database down.
// ---------------------------------------------------------------------

// Most records depend on nothing, and the ones that do depend on a
// handful. Above that the list is read into memory of its own rather than
// a bound being invented for it.
#define FLUXION_ASSET_DATABASE_MAX_INLINE_DEPENDENCIES 16

static void Fluxion_AssetDatabase_WriteText(FluxionStream* stream, const char* value)
{
    // Copied rather than written straight from the pool, because the
    // stream writes through a mutable pointer and the pool is handed out
    // as text nobody may change.
    char scratch[FLUXION_ASSET_MAX_PATH_LENGTH + 1];

    const usize length = strlen(value);
    FLUXION_ASSERT_MSG(length <= FLUXION_ASSET_MAX_PATH_LENGTH, "a stored asset string outgrew what the database accepts");

    u32 written = (u32)(length <= FLUXION_ASSET_MAX_PATH_LENGTH ? length : FLUXION_ASSET_MAX_PATH_LENGTH);
    Fluxion_Stream_SerializeU32(stream, &written);
    if (written > 0)
    {
        memcpy(scratch, value, written);
        Fluxion_Stream_SerializeBytes(stream, scratch, written);
    }
}

static void Fluxion_AssetDatabase_ReadText(FluxionStream* stream, char* buffer, usize capacity)
{
    u32 length = 0;
    Fluxion_Stream_SerializeU32(stream, &length);

    if (length >= capacity)
    {
        // Too long to hold, so it is stepped over rather than truncated --
        // half a path that still looks like a path is worse than none.
        Fluxion_Stream_Skip(stream, length);
        buffer[0] = '\0';
        return;
    }

    if (length > 0) Fluxion_Stream_SerializeBytes(stream, buffer, length);
    buffer[length] = '\0';
}

bool Fluxion_AssetDatabase_SerializeFiltered(FluxionStream* stream, const FluxionAssetDatabaseWriteFilter* filter)
{
    if (!s_initialized || !stream) return false;
    if (!Fluxion_Stream_IsWriting(stream)) return false;

    const bool includeSourcePaths = filter ? filter->includeSourcePaths : true;
    void* userData = filter ? filter->userData : NULL;

    u32 formatVersion = FLUXION_ASSET_DATABASE_VERSION;
    Fluxion_Stream_SerializeU32(stream, &formatVersion);

    // Counted before anything is written, because the count comes first
    // on the wire and the filter decides what the count is.
    u32 count = 0;
    for (usize i = 0; i < s_records.count; ++i)
    {
        const FluxionAssetRecord* record = (const FluxionAssetRecord*)Fluxion_DynamicArray_At(&s_records, i);
        if (!filter || !filter->shouldWrite || filter->shouldWrite(record, userData)) ++count;
    }
    Fluxion_Stream_SerializeU32(stream, &count);

    for (usize i = 0; i < s_records.count; ++i)
    {
        FluxionAssetRecord* record = (FluxionAssetRecord*)Fluxion_DynamicArray_At(&s_records, i);
        if (filter && filter->shouldWrite && !filter->shouldWrite(record, userData)) continue;

        Fluxion_Stream_SerializeBytes(stream, record->id.bytes, sizeof(record->id.bytes));
        Fluxion_Stream_SerializeU64(stream, &record->type);
        Fluxion_Stream_SerializeU32(stream, &record->version);

        Fluxion_AssetDatabase_WriteText(stream, Fluxion_AssetDatabase_GetName(record));
        Fluxion_AssetDatabase_WriteText(stream, includeSourcePaths ? Fluxion_AssetDatabase_GetSourcePath(record) : "");

        // A build that says where the bytes ended up gets ONE cooked form
        // out, under the general target -- because by then there is only
        // one: the packager picked a target and put that one in. An index
        // that still listed the others would name files the package does
        // not contain.
        if (filter && filter->cookedPathFor)
        {
            const char* shipped = filter->cookedPathFor(record, userData);

            u32 formCount = 1;
            Fluxion_Stream_SerializeU32(stream, &formCount);
            Fluxion_AssetDatabase_WriteText(stream, "");
            Fluxion_AssetDatabase_WriteText(stream, shipped ? shipped : "");
        }
        else
        {
            u32 formCount = record->cookedCount;
            Fluxion_Stream_SerializeU32(stream, &formCount);
            for (u32 f = 0; f < formCount; ++f)
            {
                Fluxion_AssetDatabase_WriteText(stream, Fluxion_AssetDatabase_GetCookedFormTargetAt(record, f));
                Fluxion_AssetDatabase_WriteText(stream, Fluxion_AssetDatabase_GetCookedFormPathAt(record, f));
            }
        }

        u32 dependencyCount = record->dependencyCount;
        Fluxion_Stream_SerializeU32(stream, &dependencyCount);
        for (u32 d = 0; d < dependencyCount; ++d)
        {
            FluxionUUID* dependency = (FluxionUUID*)Fluxion_DynamicArray_At(&s_dependencies, record->dependencyOffset + d);
            Fluxion_Stream_SerializeBytes(stream, dependency->bytes, sizeof(dependency->bytes));
        }

        // Import settings travel with the record even into a shipped
        // index. They are small, and they are what a re-import compares
        // against -- a project rebuilt from a package it shipped would
        // otherwise re-cook everything on the first run.
        u32 settingsSize = 0;
        const void* settings = Fluxion_AssetDatabase_GetImportSettings(record, &settingsSize);
        Fluxion_Stream_SerializeU32(stream, &settingsSize);
        if (settingsSize > 0)
        {
            u8 scratch[FLUXION_ASSET_MAX_IMPORT_SETTINGS_BYTES];
            memcpy(scratch, settings, settingsSize);
            Fluxion_Stream_SerializeBytes(stream, scratch, settingsSize);
        }
    }

    return !Fluxion_Stream_HasOverflowed(stream);
}

bool Fluxion_AssetDatabase_Serialize(FluxionStream* stream)
{
    if (!s_initialized || !stream) return false;

    if (Fluxion_Stream_IsWriting(stream)) return Fluxion_AssetDatabase_SerializeFiltered(stream, NULL);

    u32 formatVersion = FLUXION_ASSET_DATABASE_VERSION;
    Fluxion_Stream_SerializeU32(stream, &formatVersion);

    if (formatVersion > FLUXION_ASSET_DATABASE_VERSION)
    {
        FLUXION_LOG_ERROR(FLUXION_ASSET_DATABASE_LOG_CATEGORY,
                          "database was written by a newer build (version %u); refusing to read it", formatVersion);
        return false;
    }

    u32 count = 0;
    Fluxion_Stream_SerializeU32(stream, &count);

    Fluxion_AssetDatabase_Clear();

    for (u32 i = 0; i < count; ++i)
    {
        FluxionAssetDesc desc;
        memset(&desc, 0, sizeof(desc));

        char name[FLUXION_ASSET_MAX_NAME_LENGTH + 1];
        char sourcePath[FLUXION_ASSET_MAX_PATH_LENGTH + 1];

        FluxionUUID id;
        Fluxion_Stream_SerializeBytes(stream, id.bytes, sizeof(id.bytes));
        Fluxion_Stream_SerializeU64(stream, &desc.type);
        Fluxion_Stream_SerializeU32(stream, &desc.version);

        Fluxion_AssetDatabase_ReadText(stream, name, sizeof(name));
        Fluxion_AssetDatabase_ReadText(stream, sourcePath, sizeof(sourcePath));

        FluxionAssetCookedForm forms[FLUXION_ASSET_MAX_COOKED_FORMS];
        char formPaths[FLUXION_ASSET_MAX_COOKED_FORMS][FLUXION_ASSET_MAX_PATH_LENGTH + 1];
        u32 formCount = 0;
        Fluxion_Stream_SerializeU32(stream, &formCount);

        if (Fluxion_Stream_HasOverflowed(stream) || formCount > FLUXION_ASSET_MAX_COOKED_FORMS) return false;

        for (u32 f = 0; f < formCount; ++f)
        {
            memset(&forms[f], 0, sizeof(forms[f]));
            Fluxion_AssetDatabase_ReadText(stream, forms[f].target, sizeof(forms[f].target));
            Fluxion_AssetDatabase_ReadText(stream, formPaths[f], sizeof(formPaths[f]));
            forms[f].path = formPaths[f];
        }

        u32 dependencyCount = 0;
        Fluxion_Stream_SerializeU32(stream, &dependencyCount);

        if (Fluxion_Stream_HasOverflowed(stream)) return false;

        FluxionUUID inlineDependencies[FLUXION_ASSET_DATABASE_MAX_INLINE_DEPENDENCIES];
        FluxionUUID* dependencies = inlineDependencies;
        FluxionUUID* heapDependencies = NULL;

        if (dependencyCount > FLUXION_ASSET_DATABASE_MAX_INLINE_DEPENDENCIES)
        {
            heapDependencies = (FluxionUUID*)Fluxion_Allocator_Alloc(s_allocator, dependencyCount * sizeof(FluxionUUID), FLUXION_DEFAULT_ALIGNMENT);
            if (!heapDependencies) return false;
            dependencies = heapDependencies;
        }

        for (u32 d = 0; d < dependencyCount; ++d)
        {
            Fluxion_Stream_SerializeBytes(stream, dependencies[d].bytes, sizeof(dependencies[d].bytes));
        }

        u32 settingsSize = 0;
        Fluxion_Stream_SerializeU32(stream, &settingsSize);

        u8 settings[FLUXION_ASSET_MAX_IMPORT_SETTINGS_BYTES];
        if (settingsSize > FLUXION_ASSET_MAX_IMPORT_SETTINGS_BYTES)
        {
            if (heapDependencies) Fluxion_Allocator_Free(s_allocator, heapDependencies, dependencyCount * sizeof(FluxionUUID));
            return false;
        }
        if (settingsSize > 0) Fluxion_Stream_SerializeBytes(stream, settings, settingsSize);

        desc.id = id;
        desc.name = name;
        desc.sourcePath = sourcePath;
        desc.cookedForms = (formCount > 0) ? forms : NULL;
        desc.cookedFormCount = formCount;
        desc.dependencies = dependencies;
        desc.dependencyCount = dependencyCount;
        desc.importSettings = (settingsSize > 0) ? settings : NULL;
        desc.importSettingsSize = settingsSize;

        const bool added = Fluxion_AssetDatabase_Add(&desc, NULL);

        if (heapDependencies) Fluxion_Allocator_Free(s_allocator, heapDependencies, dependencyCount * sizeof(FluxionUUID));
        if (!added || Fluxion_Stream_HasOverflowed(stream)) return false;
    }

    return true;
}
