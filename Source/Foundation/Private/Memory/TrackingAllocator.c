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

#include <Fluxion/Foundation/Memory/TrackingAllocator.h>

#include <Fluxion/Foundation/Memory/MemoryTracker.h>

static void* Fluxion_TrackingAllocator_Alloc(FluxionAllocator* self, usize size, usize alignment)
{
    FluxionAllocator* inner = (FluxionAllocator*)self->userData;
    void* block = Fluxion_Allocator_Alloc(inner, size, alignment);
    if (block)
    {
        Fluxion_MemoryTracker_RecordAlloc(Fluxion_MemoryTracker_GetCurrentDomain(), size);
    }
    return block;
}

static void* Fluxion_TrackingAllocator_Realloc(FluxionAllocator* self, void* block, usize oldSize, usize newSize, usize alignment)
{
    FluxionAllocator* inner = (FluxionAllocator*)self->userData;
    void* newBlock = Fluxion_Allocator_Realloc(inner, block, oldSize, newSize, alignment);
    if (newBlock)
    {
        FluxionMemoryDomainId domain = Fluxion_MemoryTracker_GetCurrentDomain();
        if (block)
        {
            // A real resize, not a fresh allocation via realloc(NULL, ...)
            // semantics -- only then was oldSize actually tracked before.
            Fluxion_MemoryTracker_RecordFree(domain, oldSize);
        }
        Fluxion_MemoryTracker_RecordAlloc(domain, newSize);
    }
    return newBlock;
}

static void Fluxion_TrackingAllocator_Free(FluxionAllocator* self, void* block, usize size)
{
    FluxionAllocator* inner = (FluxionAllocator*)self->userData;
    Fluxion_Allocator_Free(inner, block, size);
    Fluxion_MemoryTracker_RecordFree(Fluxion_MemoryTracker_GetCurrentDomain(), size);
}

void Fluxion_TrackingAllocator_Init(FluxionAllocator* out, FluxionAllocator* inner)
{
    out->alloc = Fluxion_TrackingAllocator_Alloc;
    out->realloc = Fluxion_TrackingAllocator_Realloc;
    out->free = Fluxion_TrackingAllocator_Free;
    out->userData = inner;
}
