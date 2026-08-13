#pragma once

#include <Fluxion/Core/Reflection/TypeId.h>
#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fixed C signature every reflected method's invoker must match,
// regardless of what the method actually takes and returns -- the same
// arrangement FluxionPropertyInfo's accessor mode already uses for
// getters and setters.
//
// `instance` points at the object the call runs on, and is NULL for a
// static method. `args` points at `parameterCount` pointers, one per
// declared parameter in order, each addressing storage of that
// parameter's own type; it may be NULL when the method takes nothing.
// `returnValue` points at storage of the return type, and is NULL for a
// method returning nothing. The caller owns all of that storage and the
// invoker only reads and writes through it, so nothing crossing this
// boundary is ever allocated on one side and released on the other.
typedef void (*FluxionMethodInvokeFn)(void* instance, void** args, void* returnValue);

typedef u32 FluxionMethodFlags;

#define FLUXION_METHOD_FLAG_NONE   ((FluxionMethodFlags)0)
#define FLUXION_METHOD_FLAG_STATIC FLUXION_BIT(0)

typedef struct FluxionMethodInfo
{
    FluxionStringView name;

    // FLUXION_TYPE_ID_INVALID for a method that returns nothing.
    FluxionTypeId returnType;

    const FluxionTypeId* parameterTypes;
    u32 parameterCount;

    FluxionMethodFlags flags;
    FluxionMethodInvokeFn invoke;
} FluxionMethodInfo;

#ifdef __cplusplus
}
#endif
