#pragma once

#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Hashing.h>
#include <Fluxion/Foundation/StableId.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

FLUXION_DEFINE_STABLE_ID(FluxionSubsystemId, SubsystemId)

#define FLUXION_SUBSYSTEM_ID_INVALID ((FluxionSubsystemId)0)

#ifdef __cplusplus
}
#endif

// Computes a FluxionSubsystemId from a name at the call site, e.g.
// FLUXION_SUBSYSTEM_ID_OF(Input). Hashes at runtime (not a compile-time
// constant) -- like FLUXION_TYPE_ID_OF, only safe to use inside a function
// (automatic storage), not as file-scope `static const` data.
#define FLUXION_SUBSYSTEM_ID_OF(Name) Fluxion_SubsystemId_FromName(Fluxion_StringView_FromCStr(#Name))
