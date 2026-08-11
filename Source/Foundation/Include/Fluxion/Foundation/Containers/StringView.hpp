#pragma once

#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Types.h>

#include <string_view>

namespace Fluxion::Foundation
{

inline std::string_view ToStdStringView(FluxionStringView view)
{
    return std::string_view(view.data, view.length);
}

inline FluxionStringView FromStdStringView(std::string_view view)
{
    return Fluxion_StringView_Make(view.data(), view.size());
}

} // namespace Fluxion::Foundation

// Free begin()/end() over FluxionStringView itself, in the global
// namespace (where FluxionStringView is declared on the C side) so
// argument-dependent lookup finds them for `for (char c : someView)`
// without needing a ToStdStringView conversion first.
inline const char* begin(const FluxionStringView& view)
{
    return view.data;
}

inline const char* end(const FluxionStringView& view)
{
    return view.data + view.length;
}
