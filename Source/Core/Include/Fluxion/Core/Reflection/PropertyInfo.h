#pragma once

#include <Fluxion/Core/Reflection/PropertyFlags.h>
#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Types.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FluxionPropertyInfo
{
    FluxionStringView name;
    FluxionTypeId type;
    usize offset;
    usize size;
    FluxionPropertyFlags flags;
} FluxionPropertyInfo;

#ifdef __cplusplus
}
#endif

// Builds one FluxionPropertyInfo initializer for use inside a
// `static const FluxionPropertyInfo[] = { ... };` array. Plain
// brace-initialization (not a compound literal), so it works unchanged in
// both C and C++.
#define FLUXION_REFLECT_PROPERTY(Type, Field, PropertyTypeId, flags) \
    { \
        Fluxion_StringView_FromCStr(#Field), \
        (PropertyTypeId), \
        offsetof(Type, Field), \
        sizeof(((Type*)0)->Field), \
        (flags) \
    }
