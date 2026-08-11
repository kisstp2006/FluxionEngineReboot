#pragma once

#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Hashing.h>
#include <Fluxion/Foundation/StableId.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

FLUXION_DEFINE_STABLE_ID(FluxionTypeId, TypeId)

#define FLUXION_TYPE_ID_INVALID ((FluxionTypeId)0)

#ifdef __cplusplus
}
#endif

// Computes a FluxionTypeId from a type/enum name at the call site, e.g.
// FLUXION_TYPE_ID_OF(MaterialSettings). This hashes at runtime (not a
// compile-time constant), so anything built with it — including
// FLUXION_REFLECT_PROPERTY below — must be assembled inside a function
// (automatic storage, e.g. a per-type RegisterXReflection() called once at
// startup), not as file-scope `static const` data.
#define FLUXION_TYPE_ID_OF(Name) Fluxion_TypeId_FromName(Fluxion_StringView_FromCStr(#Name))
