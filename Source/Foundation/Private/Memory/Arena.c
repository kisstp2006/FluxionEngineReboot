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

#include <Fluxion/Foundation/Memory/Arena.h>

#include <Fluxion/Foundation/Assert.h>

#include <stddef.h>

static usize Fluxion_AlignUp(usize value, usize alignment)
{
    return (value + (alignment - 1)) & ~(alignment - 1);
}

void Fluxion_Arena_Init(FluxionArena* arena, FluxionAllocator* backingAllocator, usize capacity)
{
    FLUXION_ASSERT(arena != NULL);

    FluxionAllocator* allocator = backingAllocator ? backingAllocator : Fluxion_DefaultAllocator();

    arena->backingAllocator = allocator;
    arena->base = (u8*)Fluxion_Allocator_Alloc(allocator, capacity, FLUXION_DEFAULT_ALIGNMENT);
    arena->capacity = capacity;
    arena->offset = 0;
}

void Fluxion_Arena_Destroy(FluxionArena* arena)
{
    FLUXION_ASSERT(arena != NULL);

    if (arena->base)
    {
        Fluxion_Allocator_Free(arena->backingAllocator, arena->base, arena->capacity);
    }
    arena->base = NULL;
    arena->capacity = 0;
    arena->offset = 0;
}

void* Fluxion_Arena_Alloc(FluxionArena* arena, usize size, usize alignment)
{
    FLUXION_ASSERT(arena != NULL);

    usize alignedOffset = Fluxion_AlignUp(arena->offset, alignment);
    if (alignedOffset + size > arena->capacity)
    {
        return NULL;
    }

    void* pointer = arena->base + alignedOffset;
    arena->offset = alignedOffset + size;
    return pointer;
}

void Fluxion_Arena_Reset(FluxionArena* arena)
{
    FLUXION_ASSERT(arena != NULL);
    arena->offset = 0;
}
