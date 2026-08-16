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

#include <Fluxion/Application/Events/EventQueue.h>

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Bit.h>

void Fluxion_EventQueue_Init(FluxionEventQueue* queue, FluxionAllocator* allocator, usize capacity)
{
    FLUXION_ASSERT(queue != NULL);
    FLUXION_ASSERT(capacity > 0);

    usize roundedCapacity = Fluxion_NextPowerOfTwo32((u32)capacity);

    queue->allocator = allocator ? allocator : Fluxion_DefaultAllocator();
    queue->events = (FluxionEvent*)Fluxion_Allocator_Alloc(queue->allocator, roundedCapacity * sizeof(FluxionEvent), FLUXION_DEFAULT_ALIGNMENT);
    queue->capacityMask = roundedCapacity - 1;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
}

void Fluxion_EventQueue_Destroy(FluxionEventQueue* queue)
{
    FLUXION_ASSERT(queue != NULL);

    if (queue->events)
    {
        Fluxion_Allocator_Free(queue->allocator, queue->events, (queue->capacityMask + 1) * sizeof(FluxionEvent));
    }
    queue->events = NULL;
    queue->capacityMask = 0;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
}

bool Fluxion_EventQueue_Push(FluxionEventQueue* queue, const FluxionEvent* event)
{
    FLUXION_ASSERT(queue != NULL);

    if (queue->count > queue->capacityMask) // count == capacity: full
    {
        return false;
    }

    queue->events[queue->tail] = *event;
    queue->tail = (queue->tail + 1) & queue->capacityMask;
    queue->count += 1;
    return true;
}

bool Fluxion_EventQueue_Pop(FluxionEventQueue* queue, FluxionEvent* outEvent)
{
    FLUXION_ASSERT(queue != NULL);

    if (queue->count == 0)
    {
        return false;
    }

    *outEvent = queue->events[queue->head];
    queue->head = (queue->head + 1) & queue->capacityMask;
    queue->count -= 1;
    return true;
}
