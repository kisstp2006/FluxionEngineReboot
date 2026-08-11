#pragma once

#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/Hashing.h>
#include <Fluxion/Foundation/StableId.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

FLUXION_DEFINE_STABLE_ID(FluxionServiceId, ServiceId)

#define FLUXION_SERVICE_ID_INVALID ((FluxionServiceId)0)

#ifdef __cplusplus
}
#endif

// Computes a FluxionServiceId from a name at the call site, e.g.
// FLUXION_SERVICE_ID_OF(Memory). Hashes at runtime (not a compile-time
// constant) -- like FLUXION_TYPE_ID_OF, only safe to use inside a function
// (automatic storage), not as file-scope `static const` data.
#define FLUXION_SERVICE_ID_OF(Name) Fluxion_ServiceId_FromName(Fluxion_StringView_FromCStr(#Name))
