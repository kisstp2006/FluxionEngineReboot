#pragma once

#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Generic, byte-addressed growable array. Not type-safe by itself — callers
// are expected to wrap it per element type at the call site (or use it
// directly with Fluxion_DynamicArray_At + a cast). Foundation stays
// C-generic here; higher layers can add type-safe ergonomics later.
typedef struct FluxionDynamicArray
{
    FluxionAllocator* allocator;
    u8* data;
    usize count;
    usize capacity;
    usize elementSize;
} FluxionDynamicArray;

void  Fluxion_DynamicArray_Init(FluxionDynamicArray* array, FluxionAllocator* allocator, usize elementSize);
void  Fluxion_DynamicArray_Destroy(FluxionDynamicArray* array);
void  Fluxion_DynamicArray_Reserve(FluxionDynamicArray* array, usize newCapacity);

// Copies *element (elementSize bytes) into the array and returns a pointer
// to the newly stored slot. Pass NULL to reserve an uninitialized slot.
void* Fluxion_DynamicArray_Push(FluxionDynamicArray* array, const void* element);

void  Fluxion_DynamicArray_Pop(FluxionDynamicArray* array);
void* Fluxion_DynamicArray_At(FluxionDynamicArray* array, usize index);
void  Fluxion_DynamicArray_Clear(FluxionDynamicArray* array);

#ifdef __cplusplus
}
#endif
