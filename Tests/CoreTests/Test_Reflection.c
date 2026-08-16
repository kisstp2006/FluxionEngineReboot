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

#include "TestFramework.h"

#include <Fluxion/Core/Reflection/EnumInfo.h>
#include <Fluxion/Core/Reflection/MethodInfo.h>
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

// A hand-written invoker, which is what the C side has: `args` addresses
// storage of each declared parameter's own type, in order, and the result
// is written through `returnValue`.
static void TestVec2_Scale_Invoke(void* instance, void** args, void* returnValue)
{
    TestVec2* self = (TestVec2*)instance;
    const f32 factor = *(const f32*)args[0];
    self->x *= factor;
    self->y *= factor;
    *(f32*)returnValue = self->x + self->y;
}

static void TestVec2_Offset_Invoke(void* instance, void** args, void* returnValue)
{
    TestVec2* self = (TestVec2*)instance;
    self->x += *(const f32*)args[0];
    self->y += *(const f32*)args[1];
    (void)returnValue;
}

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

    const FluxionTypeId scaleParameters[] = { FLUXION_TYPE_ID_OF(f32) };
    const FluxionTypeId offsetParameters[] = { FLUXION_TYPE_ID_OF(f32), FLUXION_TYPE_ID_OF(f32) };

    FluxionMethodInfo vec2Methods[2];
    vec2Methods[0].name = Fluxion_StringView_FromCStr("Scale");
    vec2Methods[0].returnType = FLUXION_TYPE_ID_OF(f32);
    vec2Methods[0].parameterTypes = scaleParameters;
    vec2Methods[0].parameterCount = 1;
    vec2Methods[0].flags = FLUXION_METHOD_FLAG_NONE;
    vec2Methods[0].invoke = TestVec2_Scale_Invoke;

    vec2Methods[1].name = Fluxion_StringView_FromCStr("Offset");
    vec2Methods[1].returnType = FLUXION_TYPE_ID_INVALID;
    vec2Methods[1].parameterTypes = offsetParameters;
    vec2Methods[1].parameterCount = 2;
    vec2Methods[1].flags = FLUXION_METHOD_FLAG_NONE;
    vec2Methods[1].invoke = TestVec2_Offset_Invoke;

    FluxionTypeInfo vec2Type;
    vec2Type.name = Fluxion_StringView_FromCStr("TestVec2");
    vec2Type.id = FLUXION_TYPE_ID_OF(TestVec2);
    vec2Type.kind = FLUXION_TYPE_KIND_STRUCT;
    vec2Type.size = sizeof(TestVec2);
    vec2Type.version = 1;
    vec2Type.members = Fluxion_Span_Make(vec2Properties, FLUXION_ARRAY_COUNT(vec2Properties), sizeof(FluxionPropertyInfo));
    vec2Type.methods = Fluxion_Span_Make(vec2Methods, FLUXION_ARRAY_COUNT(vec2Methods), sizeof(FluxionMethodInfo));

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

    // Methods are enumerated off the registered descriptor and actually
    // called through it: metadata that cannot be used is metadata that
    // cannot be trusted.
    TEST_CHECK(ctx, foundById != NULL && foundById->methods.count == 2);
    {
        const FluxionMethodInfo* scale = (const FluxionMethodInfo*)Fluxion_Span_At(foundById->methods, 0);
        TEST_CHECK(ctx, Fluxion_StringView_Equals(scale->name, Fluxion_StringView_FromCStr("Scale")));
        TEST_CHECK(ctx, scale->parameterCount == 1);
        TEST_CHECK(ctx, scale->parameterTypes[0] == FLUXION_TYPE_ID_OF(f32));
        TEST_CHECK(ctx, scale->returnType == FLUXION_TYPE_ID_OF(f32));
        TEST_CHECK(ctx, (scale->flags & FLUXION_METHOD_FLAG_STATIC) == 0);

        f32 factor = 2.0f;
        void* scaleArgs[1];
        scaleArgs[0] = &factor;
        f32 scaleResult = 0.0f;
        scale->invoke(&instance, scaleArgs, &scaleResult);

        TEST_CHECK(ctx, instance.x == 6.0f);
        TEST_CHECK(ctx, instance.y == 8.0f);
        TEST_CHECK(ctx, scaleResult == 14.0f);

        const FluxionMethodInfo* offset = (const FluxionMethodInfo*)Fluxion_Span_At(foundById->methods, 1);
        TEST_CHECK(ctx, Fluxion_StringView_Equals(offset->name, Fluxion_StringView_FromCStr("Offset")));
        TEST_CHECK(ctx, offset->parameterCount == 2);
        TEST_CHECK(ctx, offset->returnType == FLUXION_TYPE_ID_INVALID);

        f32 dx = 1.0f;
        f32 dy = 2.0f;
        void* offsetArgs[2];
        offsetArgs[0] = &dx;
        offsetArgs[1] = &dy;
        offset->invoke(&instance, offsetArgs, NULL);

        // Each argument reached the parameter it was meant for, so the
        // two did not swap places on the way across.
        TEST_CHECK(ctx, instance.x == 7.0f);
        TEST_CHECK(ctx, instance.y == 10.0f);
    }

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
    colorType.methods = Fluxion_Span_Make(NULL, 0, sizeof(FluxionMethodInfo));

    TEST_CHECK(ctx, Fluxion_Reflection_RegisterType(&colorType));

    const FluxionTypeInfo* foundColorType = Fluxion_Reflection_FindTypeById(colorType.id);
    TEST_CHECK(ctx, foundColorType != NULL && foundColorType->kind == FLUXION_TYPE_KIND_ENUM);
    TEST_CHECK(ctx, foundColorType != NULL && foundColorType->members.count == 3);

    const FluxionEnumValueInfo* greenValue = (const FluxionEnumValueInfo*)Fluxion_Span_At(foundColorType->members, 1);
    TEST_CHECK(ctx, Fluxion_StringView_Equals(greenValue->name, Fluxion_StringView_FromCStr("TEST_COLOR_GREEN")));
    TEST_CHECK(ctx, greenValue->value == (i64)TEST_COLOR_GREEN);

    Fluxion_Reflection_Shutdown();
}
