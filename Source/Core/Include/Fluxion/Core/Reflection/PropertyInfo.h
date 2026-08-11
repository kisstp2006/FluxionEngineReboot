#pragma once

#include <Fluxion/Core/Reflection/PropertyFlags.h>
#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Types.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FluxionPropertyAccessKind
{
    FLUXION_PROPERTY_ACCESS_OFFSET = 0,
    FLUXION_PROPERTY_ACCESS_ACCESSOR,
} FluxionPropertyAccessKind;

// Fixed C signature every accessor-mode property's getter/setter must
// match, regardless of the property's actual type -- outValue/value point
// at storage of that actual type. The C++ facade (Reflection.hpp) builds
// these as trampolines wrapping real getter/setter member functions.
typedef void (*FluxionPropertyGetterFn)(const void* instance, void* outValue);
typedef void (*FluxionPropertySetterFn)(void* instance, const void* value);

typedef struct FluxionPropertyInfo
{
    FluxionStringView name;
    FluxionTypeId type;
    usize size;
    FluxionPropertyFlags flags;
    FluxionPropertyAccessKind accessKind;

    // Anonymous union: `.offset` stays a direct field on
    // FluxionPropertyInfo (not `.something.offset`), so every existing
    // offset-mode call site keeps compiling unchanged -- this is a purely
    // additive extension, not a breaking change.
    union
    {
        usize offset;
        struct
        {
            FluxionPropertyGetterFn getter;
            FluxionPropertySetterFn setter;
        } accessor;
    };
} FluxionPropertyInfo;

#ifdef __cplusplus
}
#endif

// Builds one offset-mode FluxionPropertyInfo initializer for use inside a
// `static const FluxionPropertyInfo[] = { ... };` array. Plain
// brace-initialization (not a compound literal), so it works unchanged in
// both C and C++. The trailing `{ offsetof(Type, Field) }` initializes
// the anonymous union's first alternative (`offset`) positionally --
// deliberately not a designated initializer, since C++ doesn't allow
// designating a member of an anonymous union the way C does.
#define FLUXION_REFLECT_PROPERTY(Type, Field, PropertyTypeId, flags) \
    { \
        Fluxion_StringView_FromCStr(#Field), \
        (PropertyTypeId), \
        sizeof(((Type*)0)->Field), \
        (flags), \
        FLUXION_PROPERTY_ACCESS_OFFSET, \
        { offsetof(Type, Field) } \
    }
