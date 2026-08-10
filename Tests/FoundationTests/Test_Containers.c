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
