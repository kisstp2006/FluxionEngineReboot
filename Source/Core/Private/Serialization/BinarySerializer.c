#include <Fluxion/Core/Serialization/BinarySerializer.h>

#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Foundation/Hashing.h>

#include <string.h>

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

static bool Fluxion_BinarySerializer_Write(FluxionStream* stream, const FluxionTypeInfo* typeInfo, void* instance)
{
    u64 id = typeInfo->id;
    u32 version = typeInfo->version;
    u32 propertyCount = (u32)typeInfo->members.count;

    Fluxion_Stream_SerializeU64(stream, &id);
    Fluxion_Stream_SerializeU32(stream, &version);
    Fluxion_Stream_SerializeU32(stream, &propertyCount);

    for (usize i = 0; i < typeInfo->members.count; ++i)
    {
        const FluxionPropertyInfo* property = (const FluxionPropertyInfo*)Fluxion_Span_At(typeInfo->members, i);
        FLUXION_ASSERT_MSG(property->size <= FLUXION_BINARY_SERIALIZER_MAX_PROPERTY_SIZE, "reflected property too large for BinarySerializer");

        u32 nameHash = Fluxion_HashBytes32(property->name.data, property->name.length);
        u32 size = (u32)property->size;
        Fluxion_Stream_SerializeU32(stream, &nameHash);
        Fluxion_Stream_SerializeU32(stream, &size);

        u8 valueBuffer[FLUXION_BINARY_SERIALIZER_MAX_PROPERTY_SIZE];
        Fluxion_BinarySerializer_ReadPropertyBytes(property, instance, valueBuffer);
        Fluxion_Stream_SerializeBytes(stream, valueBuffer, property->size);
    }

    return !Fluxion_Stream_HasOverflowed(stream);
}

static bool Fluxion_BinarySerializer_Read(FluxionStream* stream, const FluxionTypeInfo* typeInfo, void* instance)
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

        if (property != NULL && property->size == (usize)size)
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

bool Fluxion_BinarySerializer_Serialize(FluxionStream* stream, const FluxionTypeInfo* typeInfo, void* instance)
{
    if (Fluxion_Stream_IsWriting(stream))
    {
        return Fluxion_BinarySerializer_Write(stream, typeInfo, instance);
    }
    return Fluxion_BinarySerializer_Read(stream, typeInfo, instance);
}
