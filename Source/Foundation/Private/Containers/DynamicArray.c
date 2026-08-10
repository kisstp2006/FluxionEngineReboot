#include <Fluxion/Foundation/Containers/DynamicArray.h>

#include <Fluxion/Foundation/Assert.h>

#include <stddef.h>
#include <string.h>

void Fluxion_DynamicArray_Init(FluxionDynamicArray* array, FluxionAllocator* allocator, usize elementSize)
{
    FLUXION_ASSERT(array != NULL);
    FLUXION_ASSERT(elementSize > 0);

    array->allocator = allocator ? allocator : Fluxion_DefaultAllocator();
    array->data = NULL;
    array->count = 0;
    array->capacity = 0;
    array->elementSize = elementSize;
}

void Fluxion_DynamicArray_Destroy(FluxionDynamicArray* array)
{
    FLUXION_ASSERT(array != NULL);

    if (array->data)
    {
        Fluxion_Allocator_Free(array->allocator, array->data, array->capacity * array->elementSize);
    }
    array->data = NULL;
    array->count = 0;
    array->capacity = 0;
}

void Fluxion_DynamicArray_Reserve(FluxionDynamicArray* array, usize newCapacity)
{
    FLUXION_ASSERT(array != NULL);

    if (newCapacity <= array->capacity)
    {
        return;
    }

    usize oldSizeBytes = array->capacity * array->elementSize;
    usize newSizeBytes = newCapacity * array->elementSize;
    void* newData = Fluxion_Allocator_Realloc(array->allocator, array->data, oldSizeBytes, newSizeBytes, FLUXION_DEFAULT_ALIGNMENT);
    FLUXION_ASSERT_MSG(newData != NULL, "Fluxion_DynamicArray_Reserve: allocation failed");

    array->data = (u8*)newData;
    array->capacity = newCapacity;
}

void* Fluxion_DynamicArray_Push(FluxionDynamicArray* array, const void* element)
{
    FLUXION_ASSERT(array != NULL);

    if (array->count == array->capacity)
    {
        usize newCapacity = array->capacity == 0 ? 4 : array->capacity * 2;
        Fluxion_DynamicArray_Reserve(array, newCapacity);
    }

    void* slot = array->data + (array->count * array->elementSize);
    if (element)
    {
        memcpy(slot, element, array->elementSize);
    }
    array->count += 1;
    return slot;
}

void Fluxion_DynamicArray_Pop(FluxionDynamicArray* array)
{
    FLUXION_ASSERT(array != NULL);
    FLUXION_ASSERT(array->count > 0);
    array->count -= 1;
}

void* Fluxion_DynamicArray_At(FluxionDynamicArray* array, usize index)
{
    FLUXION_ASSERT(array != NULL);
    FLUXION_ASSERT(index < array->count);
    return array->data + (index * array->elementSize);
}

void Fluxion_DynamicArray_Clear(FluxionDynamicArray* array)
{
    FLUXION_ASSERT(array != NULL);
    array->count = 0;
}
