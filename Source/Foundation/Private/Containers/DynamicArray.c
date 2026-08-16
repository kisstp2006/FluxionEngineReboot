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
