#pragma once

#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Generic {pointer,count} view over a contiguous byte range. Element size
// is tracked so the same struct can describe a span of any element type;
// callers cast `data` back to the appropriate pointer type at the call
// site. C++ code that wants a type-safe span can use std::span directly
// instead.
typedef struct FluxionSpan
{
    void* data;
    usize count;
    usize elementSize;
} FluxionSpan;

static inline FluxionSpan Fluxion_Span_Make(void* data, usize count, usize elementSize)
{
    FluxionSpan span;
    span.data = data;
    span.count = count;
    span.elementSize = elementSize;
    return span;
}

static inline void* Fluxion_Span_At(FluxionSpan span, usize index)
{
    return (u8*)span.data + (index * span.elementSize);
}

#ifdef __cplusplus
}
#endif
