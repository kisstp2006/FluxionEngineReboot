#include "TestFramework.h"

#include <Fluxion/Core/Reflection/MethodInfo.h>
#include <Fluxion/Core/Reflection/PropertyFlags.h>
#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Core/Reflection/TypeInfo.h>
#include <Fluxion/Core/Serialization/BinarySerializer.h>
#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Defines.h>

typedef struct TestSerializedVec2
{
    f32 x;
    f32 y;
} TestSerializedVec2;

typedef struct TestStructV1
{
    f32 x;
    f32 y;
} TestStructV1;

typedef struct TestStructV2
{
    f32 x;
    f32 y;
    f32 z;
} TestStructV2;

void Test_BinarySerializer_Run(TestContext* ctx)
{
    // Basic roundtrip.
    {
        FluxionPropertyInfo properties[] =
        {
            FLUXION_REFLECT_PROPERTY(TestSerializedVec2, x, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
            FLUXION_REFLECT_PROPERTY(TestSerializedVec2, y, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
        };
        FluxionTypeInfo typeInfo;
        typeInfo.name = Fluxion_StringView_FromCStr("TestSerializedVec2");
        typeInfo.id = FLUXION_TYPE_ID_OF(TestSerializedVec2);
        typeInfo.kind = FLUXION_TYPE_KIND_STRUCT;
        typeInfo.size = sizeof(TestSerializedVec2);
        typeInfo.version = 1;
        typeInfo.members = Fluxion_Span_Make(properties, FLUXION_ARRAY_COUNT(properties), sizeof(FluxionPropertyInfo));
        typeInfo.methods = Fluxion_Span_Make(NULL, 0, sizeof(FluxionMethodInfo));

        TestSerializedVec2 source = { 1.5f, -2.5f };

        u8 buffer[128];
        FluxionStream writer;
        Fluxion_MemoryStream_InitWriter(&writer, buffer, sizeof(buffer));
        TEST_CHECK(ctx, Fluxion_BinarySerializer_Serialize(&writer, &typeInfo, &source));

        TestSerializedVec2 loaded = { 0.0f, 0.0f };
        FluxionStream reader;
        Fluxion_MemoryStream_InitReader(&reader, buffer, Fluxion_Stream_GetPosition(&writer));
        TEST_CHECK(ctx, Fluxion_BinarySerializer_Serialize(&reader, &typeInfo, &loaded));

        TEST_CHECK(ctx, loaded.x == source.x && loaded.y == source.y);
    }

    // Backward-upgrade: same type identity (same name -> same TypeId),
    // different property sets across "versions" -- an old stream loads
    // into a newer struct (the extra field keeps its pre-set default),
    // and a new stream partially loads into an older struct (the unknown
    // tag is skipped, not an error).
    {
        const FluxionTypeId sharedId = FLUXION_TYPE_ID_OF(TestVersionedStruct);

        FluxionPropertyInfo propertiesV1[] =
        {
            FLUXION_REFLECT_PROPERTY(TestStructV1, x, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
            FLUXION_REFLECT_PROPERTY(TestStructV1, y, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
        };
        FluxionTypeInfo typeInfoV1;
        typeInfoV1.name = Fluxion_StringView_FromCStr("TestVersionedStruct");
        typeInfoV1.id = sharedId;
        typeInfoV1.kind = FLUXION_TYPE_KIND_STRUCT;
        typeInfoV1.size = sizeof(TestStructV1);
        typeInfoV1.version = 1;
        typeInfoV1.members = Fluxion_Span_Make(propertiesV1, FLUXION_ARRAY_COUNT(propertiesV1), sizeof(FluxionPropertyInfo));
        typeInfoV1.methods = Fluxion_Span_Make(NULL, 0, sizeof(FluxionMethodInfo));

        FluxionPropertyInfo propertiesV2[] =
        {
            FLUXION_REFLECT_PROPERTY(TestStructV2, x, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
            FLUXION_REFLECT_PROPERTY(TestStructV2, y, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
            FLUXION_REFLECT_PROPERTY(TestStructV2, z, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
        };
        FluxionTypeInfo typeInfoV2;
        typeInfoV2.name = Fluxion_StringView_FromCStr("TestVersionedStruct");
        typeInfoV2.id = sharedId;
        typeInfoV2.kind = FLUXION_TYPE_KIND_STRUCT;
        typeInfoV2.size = sizeof(TestStructV2);
        typeInfoV2.version = 2;
        typeInfoV2.members = Fluxion_Span_Make(propertiesV2, FLUXION_ARRAY_COUNT(propertiesV2), sizeof(FluxionPropertyInfo));
        typeInfoV2.methods = Fluxion_Span_Make(NULL, 0, sizeof(FluxionMethodInfo));

        // Old (v1) stream -> newer (v2) struct.
        {
            TestStructV1 oldData = { 10.0f, 20.0f };
            u8 buffer[128];
            FluxionStream writer;
            Fluxion_MemoryStream_InitWriter(&writer, buffer, sizeof(buffer));
            TEST_CHECK(ctx, Fluxion_BinarySerializer_Serialize(&writer, &typeInfoV1, &oldData));

            TestStructV2 loaded = { 0.0f, 0.0f, 99.0f };
            FluxionStream reader;
            Fluxion_MemoryStream_InitReader(&reader, buffer, Fluxion_Stream_GetPosition(&writer));
            TEST_CHECK(ctx, Fluxion_BinarySerializer_Serialize(&reader, &typeInfoV2, &loaded));

            TEST_CHECK(ctx, loaded.x == 10.0f && loaded.y == 20.0f);
            TEST_CHECK(ctx, loaded.z == 99.0f); // untouched -- no "z" tag existed in the v1 stream
        }

        // New (v2) stream -> older (v1) struct.
        {
            TestStructV2 newData = { 1.0f, 2.0f, 3.0f };
            u8 buffer[128];
            FluxionStream writer;
            Fluxion_MemoryStream_InitWriter(&writer, buffer, sizeof(buffer));
            TEST_CHECK(ctx, Fluxion_BinarySerializer_Serialize(&writer, &typeInfoV2, &newData));

            TestStructV1 loaded = { 0.0f, 0.0f };
            FluxionStream reader;
            Fluxion_MemoryStream_InitReader(&reader, buffer, Fluxion_Stream_GetPosition(&writer));
            TEST_CHECK(ctx, Fluxion_BinarySerializer_Serialize(&reader, &typeInfoV1, &loaded));

            TEST_CHECK(ctx, loaded.x == 1.0f && loaded.y == 2.0f);
        }
    }

    // Mismatched type id must fail cleanly, not misinterpret the stream.
    {
        FluxionPropertyInfo properties[] =
        {
            FLUXION_REFLECT_PROPERTY(TestSerializedVec2, x, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
            FLUXION_REFLECT_PROPERTY(TestSerializedVec2, y, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_NONE),
        };
        FluxionTypeInfo typeInfoA;
        typeInfoA.name = Fluxion_StringView_FromCStr("TestSerializedVec2");
        typeInfoA.id = FLUXION_TYPE_ID_OF(TestSerializedVec2);
        typeInfoA.kind = FLUXION_TYPE_KIND_STRUCT;
        typeInfoA.size = sizeof(TestSerializedVec2);
        typeInfoA.version = 1;
        typeInfoA.members = Fluxion_Span_Make(properties, FLUXION_ARRAY_COUNT(properties), sizeof(FluxionPropertyInfo));
        typeInfoA.methods = Fluxion_Span_Make(NULL, 0, sizeof(FluxionMethodInfo));

        FluxionTypeInfo typeInfoUnrelated = typeInfoA;
        typeInfoUnrelated.id = FLUXION_TYPE_ID_OF(SomeUnrelatedType);

        TestSerializedVec2 source = { 1.0f, 2.0f };
        u8 buffer[128];
        FluxionStream writer;
        Fluxion_MemoryStream_InitWriter(&writer, buffer, sizeof(buffer));
        TEST_CHECK(ctx, Fluxion_BinarySerializer_Serialize(&writer, &typeInfoA, &source));

        TestSerializedVec2 loaded = { -1.0f, -1.0f };
        FluxionStream reader;
        Fluxion_MemoryStream_InitReader(&reader, buffer, Fluxion_Stream_GetPosition(&writer));
        TEST_CHECK(ctx, Fluxion_BinarySerializer_Serialize(&reader, &typeInfoUnrelated, &loaded) == false);
        TEST_CHECK(ctx, loaded.x == -1.0f && loaded.y == -1.0f); // untouched
    }
}
