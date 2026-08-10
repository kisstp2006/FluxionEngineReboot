#include "TestFramework.h"

#include <Fluxion/Core/Reflection/EnumInfo.h>
#include <Fluxion/Core/Reflection/PropertyInfo.h>
#include <Fluxion/Core/Reflection/Registry.h>
#include <Fluxion/Core/Reflection/TypeInfo.h>
#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Types.h>

typedef struct TestVec2
{
    f32 x;
    f32 y;
} TestVec2;

typedef enum TestColorChannel
{
    TEST_COLOR_RED = 0,
    TEST_COLOR_GREEN,
    TEST_COLOR_BLUE,
} TestColorChannel;

void Test_Reflection_Run(TestContext* ctx)
{
    Fluxion_Reflection_Init();

    // FLUXION_TYPE_ID_OF/FLUXION_REFLECT_PROPERTY hash and compute
    // offsets at runtime, so these arrays are built as local (automatic
    // storage) data inside this function, not as file-scope `static
    // const` — see the comment on FLUXION_TYPE_ID_OF in TypeId.h.
    FluxionPropertyInfo vec2Properties[] =
    {
        FLUXION_REFLECT_PROPERTY(TestVec2, x, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_EDITOR_VISIBLE),
        FLUXION_REFLECT_PROPERTY(TestVec2, y, FLUXION_TYPE_ID_OF(f32), FLUXION_PROPERTY_FLAG_EDITOR_VISIBLE),
    };

    FluxionTypeInfo vec2Type;
    vec2Type.name = Fluxion_StringView_FromCStr("TestVec2");
    vec2Type.id = FLUXION_TYPE_ID_OF(TestVec2);
    vec2Type.kind = FLUXION_TYPE_KIND_STRUCT;
    vec2Type.size = sizeof(TestVec2);
    vec2Type.version = 1;
    vec2Type.members = Fluxion_Span_Make(vec2Properties, FLUXION_ARRAY_COUNT(vec2Properties), sizeof(FluxionPropertyInfo));

    TEST_CHECK(ctx, Fluxion_Reflection_RegisterType(&vec2Type));

    const FluxionTypeInfo* foundById = Fluxion_Reflection_FindTypeById(vec2Type.id);
    TEST_CHECK(ctx, foundById == &vec2Type);

    const FluxionTypeInfo* foundByName = Fluxion_Reflection_FindTypeByName(Fluxion_StringView_FromCStr("TestVec2"));
    TEST_CHECK(ctx, foundByName == &vec2Type);

    const FluxionTypeInfo* missing = Fluxion_Reflection_FindTypeByName(Fluxion_StringView_FromCStr("DoesNotExist"));
    TEST_CHECK(ctx, missing == NULL);

    TEST_CHECK(ctx, foundById != NULL && foundById->kind == FLUXION_TYPE_KIND_STRUCT);
    TEST_CHECK(ctx, foundById != NULL && foundById->members.count == 2);

    // Generic field read through the reflected offset: prove the metadata
    // is actually usable, not just stored.
    TestVec2 instance = { 3.0f, 4.0f };
    const FluxionPropertyInfo* yProperty = (const FluxionPropertyInfo*)Fluxion_Span_At(foundById->members, 1);
    TEST_CHECK(ctx, Fluxion_StringView_Equals(yProperty->name, Fluxion_StringView_FromCStr("y")));
    f32 yValue = *(f32*)((u8*)&instance + yProperty->offset);
    TEST_CHECK(ctx, yValue == 4.0f);

    // Enum reflection.
    FluxionEnumValueInfo colorValues[] =
    {
        FLUXION_REFLECT_ENUM_VALUE(TEST_COLOR_RED),
        FLUXION_REFLECT_ENUM_VALUE(TEST_COLOR_GREEN),
        FLUXION_REFLECT_ENUM_VALUE(TEST_COLOR_BLUE),
    };

    FluxionTypeInfo colorType;
    colorType.name = Fluxion_StringView_FromCStr("TestColorChannel");
    colorType.id = FLUXION_TYPE_ID_OF(TestColorChannel);
    colorType.kind = FLUXION_TYPE_KIND_ENUM;
    colorType.size = sizeof(TestColorChannel);
    colorType.version = 1;
    colorType.members = Fluxion_Span_Make(colorValues, FLUXION_ARRAY_COUNT(colorValues), sizeof(FluxionEnumValueInfo));

    TEST_CHECK(ctx, Fluxion_Reflection_RegisterType(&colorType));

    const FluxionTypeInfo* foundColorType = Fluxion_Reflection_FindTypeById(colorType.id);
    TEST_CHECK(ctx, foundColorType != NULL && foundColorType->kind == FLUXION_TYPE_KIND_ENUM);
    TEST_CHECK(ctx, foundColorType != NULL && foundColorType->members.count == 3);

    const FluxionEnumValueInfo* greenValue = (const FluxionEnumValueInfo*)Fluxion_Span_At(foundColorType->members, 1);
    TEST_CHECK(ctx, Fluxion_StringView_Equals(greenValue->name, Fluxion_StringView_FromCStr("TEST_COLOR_GREEN")));
    TEST_CHECK(ctx, greenValue->value == (i64)TEST_COLOR_GREEN);

    Fluxion_Reflection_Shutdown();
}
