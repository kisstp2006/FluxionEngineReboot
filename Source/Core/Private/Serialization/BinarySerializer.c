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

#include <Fluxion/Core/Serialization/BinarySerializer.h>

#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Foundation/Hashing.h>

#include <string.h>

// Whether this property is written out at all.
//
// TRANSIENT means a value that is worked out from others rather than
// held: writing it would save an answer that has to be recomputed on the
// way back in anyway, and one that would be wrong if what it was
// computed from were read back in a different order.
static bool Fluxion_BinarySerializer_IsSaved(const FluxionPropertyInfo* property,
                                             const FluxionTypeId* skipTypes, u32 skipCount)
{
    u32 i;
    if ((property->flags & FLUXION_PROPERTY_FLAG_TRANSIENT) != 0) return false;
    for (i = 0; i < skipCount; ++i)
    {
        if (property->type == skipTypes[i]) return false;
    }
    return true;
}

static u32 Fluxion_BinarySerializer_SavedCount(const FluxionTypeInfo* typeInfo,
                                               const FluxionTypeId* skipTypes, u32 skipCount)
{
    u32 count = 0;
    for (usize i = 0; i < typeInfo->members.count; ++i)
    {
        if (Fluxion_BinarySerializer_IsSaved((const FluxionPropertyInfo*)Fluxion_Span_At(typeInfo->members, i),
                                             skipTypes, skipCount))
        {
            ++count;
        }
    }
    return count;
}

static const FluxionPropertyInfo* Fluxion_BinarySerializer_FindPropertyByNameHash(const FluxionTypeInfo* typeInfo, u32 nameHash)
{
    for (usize i = 0; i < typeInfo->members.count; ++i)
    {
        const FluxionPropertyInfo* property = (const FluxionPropertyInfo*)Fluxion_Span_At(typeInfo->members, i);
        u32 propertyNameHash = Fluxion_HashBytes32(property->name.data, property->name.length);
        if (propertyNameHash == nameHash)
        {
            return property;
        }
    }
    return NULL;
}

static void Fluxion_BinarySerializer_ReadPropertyBytes(const FluxionPropertyInfo* property, const void* instance, void* outBuffer)
{
    if (property->accessKind == FLUXION_PROPERTY_ACCESS_OFFSET)
    {
        memcpy(outBuffer, (const u8*)instance + property->offset, property->size);
    }
    else
    {
        property->accessor.getter(instance, outBuffer, property->accessor.context);
    }
}

static void Fluxion_BinarySerializer_WritePropertyBytes(const FluxionPropertyInfo* property, void* instance, const void* value)
{
    if (property->accessKind == FLUXION_PROPERTY_ACCESS_OFFSET)
    {
        memcpy((u8*)instance + property->offset, value, property->size);
    }
    else
    {
        property->accessor.setter(instance, value, property->accessor.context);
    }
}

static bool Fluxion_BinarySerializer_Write(FluxionStream* stream, const FluxionTypeInfo* typeInfo, void* instance,
                                           const FluxionTypeId* skipTypes, u32 skipCount)
{
    u64 id = typeInfo->id;
    u32 version = typeInfo->version;
    u32 propertyCount = Fluxion_BinarySerializer_SavedCount(typeInfo, skipTypes, skipCount);

    Fluxion_Stream_SerializeU64(stream, &id);
    Fluxion_Stream_SerializeU32(stream, &version);
    Fluxion_Stream_SerializeU32(stream, &propertyCount);

    for (usize i = 0; i < typeInfo->members.count; ++i)
    {
        const FluxionPropertyInfo* property = (const FluxionPropertyInfo*)Fluxion_Span_At(typeInfo->members, i);
        u32 nameHash;
        u32 size;

        if (!Fluxion_BinarySerializer_IsSaved(property, skipTypes, skipCount)) continue;

        FLUXION_ASSERT_MSG(property->size <= FLUXION_BINARY_SERIALIZER_MAX_PROPERTY_SIZE, "reflected property too large for BinarySerializer");

        nameHash = Fluxion_HashBytes32(property->name.data, property->name.length);
        Fluxion_Stream_SerializeU32(stream, &nameHash);

        if ((property->flags & FLUXION_PROPERTY_FLAG_TEXT) != 0)
        {
            // The value is a pointer to characters, so the characters go
            // out rather than the pointer. The recorded size is the text's
            // length, which is what keeps a reader that no longer knows
            // this property able to skip exactly the right run.
            const char* text = NULL;
            u8 pointerBuffer[FLUXION_BINARY_SERIALIZER_MAX_PROPERTY_SIZE];

            Fluxion_BinarySerializer_ReadPropertyBytes(property, instance, pointerBuffer);
            memcpy(&text, pointerBuffer, sizeof(text));

            size = (text != NULL) ? (u32)strlen(text) : 0u;
            Fluxion_Stream_SerializeU32(stream, &size);
            if (size != 0) Fluxion_Stream_SerializeBytes(stream, (void*)text, size);
        }
        else
        {
            u8 valueBuffer[FLUXION_BINARY_SERIALIZER_MAX_PROPERTY_SIZE];
            size = (u32)property->size;
            Fluxion_Stream_SerializeU32(stream, &size);
            Fluxion_BinarySerializer_ReadPropertyBytes(property, instance, valueBuffer);
            Fluxion_Stream_SerializeBytes(stream, valueBuffer, property->size);
        }
    }

    return !Fluxion_Stream_HasOverflowed(stream);
}

static bool Fluxion_BinarySerializer_Read(FluxionStream* stream, const FluxionTypeInfo* typeInfo, void* instance,
                                          const FluxionTypeId* skipTypes, u32 skipCount)
{
    u64 storedId = 0;
    u32 storedVersion = 0;
    u32 storedPropertyCount = 0;

    Fluxion_Stream_SerializeU64(stream, &storedId);
    Fluxion_Stream_SerializeU32(stream, &storedVersion);
    Fluxion_Stream_SerializeU32(stream, &storedPropertyCount);

    if (Fluxion_Stream_HasOverflowed(stream)) return false;
    if (storedId != typeInfo->id) return false; // not the type this call expects to read

    for (u32 i = 0; i < storedPropertyCount; ++i)
    {
        u32 nameHash = 0;
        u32 size = 0;
        Fluxion_Stream_SerializeU32(stream, &nameHash);
        Fluxion_Stream_SerializeU32(stream, &size);

        const FluxionPropertyInfo* property = Fluxion_BinarySerializer_FindPropertyByNameHash(typeInfo, nameHash);

        // A property the writer left out cannot be read back in, and one
        // that is transient now was not meant to be: in both cases the
        // recorded run is stepped over and whatever the instance already
        // holds stays.
        if (property != NULL && !Fluxion_BinarySerializer_IsSaved(property, skipTypes, skipCount)) property = NULL;

        if (property != NULL && (property->flags & FLUXION_PROPERTY_FLAG_TEXT) != 0)
        {
            // Read into a buffer of its own and handed over null-
            // terminated, because the setter is given a pointer to
            // characters rather than the characters.
            char text[FLUXION_BINARY_SERIALIZER_MAX_TEXT_LENGTH];
            const char* pointer = text;

            if (size < sizeof(text))
            {
                Fluxion_Stream_SerializeBytes(stream, text, size);
                text[size] = '\0';
                Fluxion_BinarySerializer_WritePropertyBytes(property, instance, &pointer);
            }
            else
            {
                // Longer than anything this can hold. Stepped over rather
                // than truncated: half a name is worse than the one the
                // instance already has.
                Fluxion_Stream_Skip(stream, size);
            }
        }
        else if (property != NULL && property->size == (usize)size)
        {
            FLUXION_ASSERT_MSG(size <= FLUXION_BINARY_SERIALIZER_MAX_PROPERTY_SIZE, "reflected property too large for BinarySerializer");
            u8 valueBuffer[FLUXION_BINARY_SERIALIZER_MAX_PROPERTY_SIZE];
            Fluxion_Stream_SerializeBytes(stream, valueBuffer, size);
            Fluxion_BinarySerializer_WritePropertyBytes(property, instance, valueBuffer);
        }
        else
        {
            // Removed/renamed property, or its type/size changed --
            // skip the recorded byte count without touching `instance`,
            // leaving whatever default value was already there.
            Fluxion_Stream_Skip(stream, size);
        }
    }

    return !Fluxion_Stream_HasOverflowed(stream);
}

bool Fluxion_BinarySerializer_SerializeExcept(FluxionStream* stream, const FluxionTypeInfo* typeInfo, void* instance,
                                              const FluxionTypeId* skipTypes, u32 skipCount)
{
    if (Fluxion_Stream_IsWriting(stream))
    {
        return Fluxion_BinarySerializer_Write(stream, typeInfo, instance, skipTypes, skipCount);
    }
    return Fluxion_BinarySerializer_Read(stream, typeInfo, instance, skipTypes, skipCount);
}

bool Fluxion_BinarySerializer_Serialize(FluxionStream* stream, const FluxionTypeInfo* typeInfo, void* instance)
{
    return Fluxion_BinarySerializer_SerializeExcept(stream, typeInfo, instance, NULL, 0);
}
