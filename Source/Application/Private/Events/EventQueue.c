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
