#pragma once

#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FluxionEnumValueInfo
{
    FluxionStringView name;
    i64 value;
} FluxionEnumValueInfo;

#ifdef __cplusplus
}
#endif

// Builds one FluxionEnumValueInfo initializer for use inside a
// `static const FluxionEnumValueInfo[] = { ... };` array.
#define FLUXION_REFLECT_ENUM_VALUE(Name) \
    { Fluxion_StringView_FromCStr(#Name), (i64)(Name) }
