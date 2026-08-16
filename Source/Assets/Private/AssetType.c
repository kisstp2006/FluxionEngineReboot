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

#include <Fluxion/Assets/AssetType.h>

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Log.h>

#include <string.h>

#define FLUXION_ASSET_TYPE_LOG_CATEGORY "AssetType"

typedef struct FluxionAssetTypeSlot
{
    FluxionAssetTypeId id;
    FluxionAssetTypeDesc desc;
} FluxionAssetTypeSlot;

static FluxionAssetTypeSlot s_types[FLUXION_ASSET_MAX_TYPES];
static u32 s_typeCount = 0;
static bool s_initialized = false;

void Fluxion_AssetTypes_Init(FluxionAllocator* allocator)
{
    // Fixed capacity, so nothing is allocated -- the parameter is here so
    // this starts the same way as everything else it starts with.
    FLUXION_UNUSED(allocator);
    FLUXION_ASSERT_MSG(!s_initialized, "Fluxion_AssetTypes_Init called twice without a Shutdown in between");

    memset(s_types, 0, sizeof(s_types));
    s_typeCount = 0;
    s_initialized = true;
}

void Fluxion_AssetTypes_Shutdown(void)
{
    memset(s_types, 0, sizeof(s_types));
    s_typeCount = 0;
    s_initialized = false;
}

bool Fluxion_AssetTypes_IsInitialized(void)
{
    return s_initialized;
}

// strnlen is not in standard C, and a descriptor arrives as a fixed-size
// array that a caller could have filled without a terminator.
static usize Fluxion_AssetTypes_BoundedLength(const char* text, usize capacity)
{
    usize length = 0;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static char Fluxion_AssetTypes_LowerCase(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// `stored` is a fixed-size array out of a descriptor, so it is compared
// under its own capacity rather than trusting it to be terminated.
static bool Fluxion_AssetTypes_ExtensionEquals(const char* stored, usize storedCapacity, const char* wanted)
{
    const usize storedLength = Fluxion_AssetTypes_BoundedLength(stored, storedCapacity);
    if (storedLength == 0 || storedLength >= storedCapacity) return false;

    for (usize i = 0; i <= storedLength; ++i)
    {
        if (Fluxion_AssetTypes_LowerCase(stored[i]) != Fluxion_AssetTypes_LowerCase(wanted[i])) return false;
        if (wanted[i] == '\0') break;
    }
    return true;
}

static i32 Fluxion_AssetTypes_IndexOf(FluxionAssetTypeId id)
{
    for (u32 i = 0; i < s_typeCount; ++i)
    {
        if (s_types[i].id == id) return (i32)i;
    }
    return -1;
}

bool Fluxion_AssetTypes_Register(const FluxionAssetTypeDesc* desc)
{
    if (!s_initialized || !desc) return false;

    const usize nameLength = Fluxion_AssetTypes_BoundedLength(desc->name, sizeof(desc->name));
    if (nameLength == 0 || nameLength >= sizeof(desc->name))
    {
        FLUXION_LOG_ERROR(FLUXION_ASSET_TYPE_LOG_CATEGORY, "an asset type needs a name that fits");
        return false;
    }

    // A type nothing can load is not a type: every other part of this is
    // optional, these two are not.
    if (!desc->load || !desc->unload)
    {
        FLUXION_LOG_ERROR(FLUXION_ASSET_TYPE_LOG_CATEGORY, "asset type '%s' has no load or no unload", desc->name);
        return false;
    }

    if (desc->sourceExtensionCount > FLUXION_ASSET_MAX_SOURCE_EXTENSIONS)
    {
        FLUXION_LOG_ERROR(FLUXION_ASSET_TYPE_LOG_CATEGORY, "asset type '%s' claims more source extensions than fit", desc->name);
        return false;
    }

    const FluxionAssetTypeId id = Fluxion_AssetTypeId_FromName(Fluxion_StringView_FromCStr(desc->name));

    if (Fluxion_AssetTypes_IndexOf(id) >= 0)
    {
        FLUXION_LOG_ERROR(FLUXION_ASSET_TYPE_LOG_CATEGORY, "asset type '%s' is already registered", desc->name);
        return false;
    }

    if (s_typeCount >= FLUXION_ASSET_MAX_TYPES)
    {
        FLUXION_LOG_ERROR(FLUXION_ASSET_TYPE_LOG_CATEGORY, "no room for asset type '%s'", desc->name);
        return false;
    }

    s_types[s_typeCount].id = id;
    s_types[s_typeCount].desc = *desc;
    ++s_typeCount;
    return true;
}

bool Fluxion_AssetTypes_Unregister(FluxionAssetTypeId id)
{
    if (!s_initialized) return false;

    const i32 index = Fluxion_AssetTypes_IndexOf(id);
    if (index < 0) return false;

    const u32 lastIndex = s_typeCount - 1;
    if ((u32)index != lastIndex) s_types[index] = s_types[lastIndex];
    memset(&s_types[lastIndex], 0, sizeof(s_types[lastIndex]));
    --s_typeCount;
    return true;
}

const FluxionAssetTypeDesc* Fluxion_AssetTypes_Find(FluxionAssetTypeId id)
{
    if (!s_initialized) return NULL;
    const i32 index = Fluxion_AssetTypes_IndexOf(id);
    return index >= 0 ? &s_types[index].desc : NULL;
}

const FluxionAssetTypeDesc* Fluxion_AssetTypes_FindByName(const char* name)
{
    if (!s_initialized || !name || name[0] == '\0') return NULL;
    return Fluxion_AssetTypes_Find(Fluxion_AssetTypeId_FromName(Fluxion_StringView_FromCStr(name)));
}

FluxionAssetTypeId Fluxion_AssetTypes_FindBySourceExtension(const char* extension)
{
    if (!s_initialized || !extension || extension[0] == '\0') return FLUXION_ASSET_TYPE_ID_INVALID;

    for (u32 i = 0; i < s_typeCount; ++i)
    {
        const FluxionAssetTypeDesc* desc = &s_types[i].desc;
        for (u32 e = 0; e < desc->sourceExtensionCount; ++e)
        {
            if (Fluxion_AssetTypes_ExtensionEquals(desc->sourceExtensions[e], sizeof(desc->sourceExtensions[e]), extension))
            {
                return s_types[i].id;
            }
        }
    }
    return FLUXION_ASSET_TYPE_ID_INVALID;
}

u32 Fluxion_AssetTypes_GetCount(void)
{
    return s_initialized ? s_typeCount : 0;
}

const FluxionAssetTypeDesc* Fluxion_AssetTypes_GetAt(u32 index)
{
    if (!s_initialized || index >= s_typeCount) return NULL;
    return &s_types[index].desc;
}

FluxionAssetTypeId Fluxion_AssetTypes_GetIdAt(u32 index)
{
    if (!s_initialized || index >= s_typeCount) return FLUXION_ASSET_TYPE_ID_INVALID;
    return s_types[index].id;
}
