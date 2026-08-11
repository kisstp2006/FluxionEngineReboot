#pragma once

#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Foundation/Types.h>

#include <span>

namespace Fluxion::Foundation
{

// FluxionSpan is deliberately type-erased (element size tracked at
// runtime -- see the comment in Span.h, which already points C++ callers
// at std::span) so it can describe a span of any element type across the
// C ABI. These just convert between the two representations -- no new
// wrapper type, std::span already provides iterators/range-for/typed
// access.
template<typename T>
std::span<T> ToStdSpan(FluxionSpan span)
{
    return std::span<T>(static_cast<T*>(span.data), span.count);
}

template<typename T>
FluxionSpan FromStdSpan(std::span<T> span)
{
    return Fluxion_Span_Make(span.data(), span.size(), sizeof(T));
}

} // namespace Fluxion::Foundation
