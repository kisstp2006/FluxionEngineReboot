#pragma once

#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Foundation/Containers/Span.h>
#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FluxionTypeKind
{
    FLUXION_TYPE_KIND_PRIMITIVE = 0,
    FLUXION_TYPE_KIND_STRUCT,
    FLUXION_TYPE_KIND_ENUM,
} FluxionTypeKind;

typedef struct FluxionTypeInfo
{
    FluxionStringView name;
    FluxionTypeId id;
    FluxionTypeKind kind;
    usize size;
    u32 version;

    // For STRUCT: a span of FluxionPropertyInfo. For ENUM: a span of
    // FluxionEnumValueInfo. Unused (data=NULL, count=0) for PRIMITIVE.
    FluxionSpan members;
} FluxionTypeInfo;

#ifdef __cplusplus
}
#endif
