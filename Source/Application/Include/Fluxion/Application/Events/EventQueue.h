#pragma once

#include <Fluxion/Application/Events/Event.h>
#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fixed-capacity ring buffer: one allocation at Init, zero after —
// Push/Pop never allocate, so this stays cheap even in debug builds and
// its memory footprint is bounded and known up front. Capacity is rounded
// up to the next power of two so Push/Pop can index with a bitmask
// instead of a modulo.
typedef struct FluxionEventQueue
{
    FluxionAllocator* allocator;
    FluxionEvent* events;
    usize capacityMask; // capacity - 1 (capacity is always a power of two)
    usize head;
    usize tail;
    usize count;
} FluxionEventQueue;

void Fluxion_EventQueue_Init(FluxionEventQueue* queue, FluxionAllocator* allocator, usize capacity);
void Fluxion_EventQueue_Destroy(FluxionEventQueue* queue);

// Returns false (and leaves the queue unchanged) if the queue is full.
bool Fluxion_EventQueue_Push(FluxionEventQueue* queue, const FluxionEvent* event);

// Returns false if the queue is empty.
bool Fluxion_EventQueue_Pop(FluxionEventQueue* queue, FluxionEvent* outEvent);

static inline usize Fluxion_EventQueue_Count(const FluxionEventQueue* queue)
{
    return queue->count;
}

#ifdef __cplusplus
}
#endif
