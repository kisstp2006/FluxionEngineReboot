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

#include <Fluxion/Foundation/Containers/DynamicArray.h>
#include <Fluxion/Foundation/Containers/HashMap.h>
#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Hashing.h>

void Test_Containers_Run(TestContext* ctx)
{
    // Span
    i32 numbers[4] = { 10, 20, 30, 40 };
    FluxionSpan span = Fluxion_Span_Make(numbers, 4, sizeof(i32));
    TEST_CHECK(ctx, *(i32*)Fluxion_Span_At(span, 2) == 30);

    // StringView
    FluxionStringView hello = Fluxion_StringView_FromCStr("Hello, Fluxion!");
    FluxionStringView needle = Fluxion_StringView_FromCStr("Fluxion");
    usize foundAt = Fluxion_StringView_Find(hello, needle);
    TEST_CHECK(ctx, foundAt == 7);

    FluxionStringView sub = Fluxion_StringView_Substr(hello, 7, 7);
    TEST_CHECK(ctx, Fluxion_StringView_Equals(sub, needle));

    FluxionStringView missing = Fluxion_StringView_FromCStr("nope");
    TEST_CHECK(ctx, Fluxion_StringView_Find(hello, missing) == FLUXION_STRINGVIEW_NOT_FOUND);

    // DynamicArray
    FluxionDynamicArray array;
    Fluxion_DynamicArray_Init(&array, NULL, sizeof(i32));
    for (i32 i = 0; i < 10; ++i)
    {
        Fluxion_DynamicArray_Push(&array, &i);
    }
    TEST_CHECK(ctx, array.count == 10);
    TEST_CHECK(ctx, *(i32*)Fluxion_DynamicArray_At(&array, 5) == 5);
    Fluxion_DynamicArray_Pop(&array);
    TEST_CHECK(ctx, array.count == 9);
    Fluxion_DynamicArray_Destroy(&array);

    // HashMap: i32 -> i32, using the shared FNV-1a hash and byte-equals
    // from Hashing.h as the default callbacks.
    FluxionHashMap map;
    Fluxion_HashMap_Init(&map, NULL, sizeof(i32), sizeof(i32), Fluxion_HashBytes64, Fluxion_BytesEqual);

    for (i32 i = 0; i < 20; ++i)
    {
        i32 value = i * 100;
        TEST_CHECK(ctx, Fluxion_HashMap_Set(&map, &i, &value));
    }
    TEST_CHECK(ctx, map.count == 20);

    i32 key = 7;
    i32* found = (i32*)Fluxion_HashMap_Find(&map, &key);
    TEST_CHECK(ctx, found != NULL);
    TEST_CHECK(ctx, found != NULL && *found == 700);

    i32 missingKey = 999;
    TEST_CHECK(ctx, Fluxion_HashMap_Find(&map, &missingKey) == NULL);

    TEST_CHECK(ctx, Fluxion_HashMap_Remove(&map, &key) == true);
    TEST_CHECK(ctx, Fluxion_HashMap_Find(&map, &key) == NULL);
    TEST_CHECK(ctx, map.count == 19);
    TEST_CHECK(ctx, Fluxion_HashMap_Remove(&map, &key) == false);

    Fluxion_HashMap_Destroy(&map);
}
