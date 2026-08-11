#pragma once

#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Types.h>

namespace Fluxion::Foundation
{

// Non-owning wrapper around a FluxionAllocator* -- lets call sites take
// "an allocator" as a small, copyable value type instead of a raw
// pointer, without implying ownership (the name says so, same convention
// as Span/StringView using non-owning-signaling names).
class AllocatorRef
{
public:
    explicit AllocatorRef(FluxionAllocator* allocator = Fluxion_DefaultAllocator())
        : m_allocator(allocator)
    {
    }

    void* Alloc(usize size, usize alignment) const
    {
        return Fluxion_Allocator_Alloc(m_allocator, size, alignment);
    }

    void* Realloc(void* block, usize oldSize, usize newSize, usize alignment) const
    {
        return Fluxion_Allocator_Realloc(m_allocator, block, oldSize, newSize, alignment);
    }

    void Free(void* block, usize size) const
    {
        Fluxion_Allocator_Free(m_allocator, block, size);
    }

    FluxionAllocator* Raw() const { return m_allocator; }

private:
    FluxionAllocator* m_allocator;
};

} // namespace Fluxion::Foundation
