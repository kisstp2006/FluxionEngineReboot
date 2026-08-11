#pragma once

#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Hashing.h>
#include <Fluxion/Foundation/Types.h>

// FLUXION_DEFINE_STABLE_ID(IdType, ShortName) declares IdType as a
// distinct stable-ID type (a plain u64) plus a
// Fluxion_<ShortName>_FromName(FluxionStringView) hashing function. IDs
// are hash-based (Fluxion_HashBytes64 over a name), never a pointer and
// never a compiler RTTI hash -- stable across recompiles as long as the
// source name string is unchanged.
//
// Each concrete ID header (TypeId.h, SubsystemId.h, ServiceId.h,
// MemoryDomain.h, ...) invokes this once, then adds its own
// <PREFIX>_INVALID constant and <PREFIX>_ID_OF(Name) call-site macro as
// plain #defines -- the C preprocessor cannot generate #define
// directives from inside another macro, so those two stay one line each
// per ID type rather than being generated here too.
#define FLUXION_DEFINE_STABLE_ID(IdType, ShortName) \
    typedef u64 IdType; \
    \
    static inline IdType Fluxion_##ShortName##_FromName(FluxionStringView name) \
    { \
        return Fluxion_HashBytes64(name.data, name.length); \
    }
