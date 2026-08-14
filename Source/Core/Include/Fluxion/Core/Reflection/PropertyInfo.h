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
// `context` is whatever the property was registered with, and it is what
// lets one pair of functions serve many properties. A type whose fields
// are known when it is compiled needs none, and gets a distinct function
// per property from the C++ facade. A type whose fields are only known
// once the program runs -- a class read out of a script module, say --
// cannot have a function per field, so it registers one pair and tells
// them apart by this.
//
// Note what the two parameters separate: `instance` says WHICH object,
// `context` says WHICH field of it. Neither can stand in for the other.
typedef void (*FluxionPropertyGetterFn)(const void* instance, void* outValue, void* context);
typedef void (*FluxionPropertySetterFn)(void* instance, const void* value, void* context);

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

            // Handed back to the two above, unread by anything else. The
            // property does not own it: whoever registered the type keeps
            // it alive for as long as the type is registered, the same
            // rule the registry already states for the descriptor itself.
            void* context;
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
