#pragma once

#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/StableId.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

FLUXION_DEFINE_STABLE_ID(FluxionMemoryDomainId, MemoryDomainId)

#define FLUXION_MEMORY_DOMAIN_ID_INVALID ((FluxionMemoryDomainId)0)

// Bounds the tracker's fixed-capacity storage (MemoryTracker.c) -- zero
// heap allocation, same rationale as FLUXION_MAX_SUBSYSTEMS/
// FLUXION_MAX_SERVICES.
#define FLUXION_MAX_MEMORY_DOMAINS 64

// A domain is one node in a tree (parent = FLUXION_MEMORY_DOMAIN_ID_INVALID
// for a root domain) -- statistics roll up from children into parents.
// Both a broad category (Foundation, Core, Renderer, ...) and a
// finer-grained label under it are just nodes in this same tree, not two
// parallel hierarchies.
typedef struct FluxionMemoryDomainDesc
{
    FluxionMemoryDomainId id;
    const char* name;
    FluxionMemoryDomainId parent;
} FluxionMemoryDomainDesc;

#ifdef __cplusplus
}
#endif

// Computes a FluxionMemoryDomainId from a name at the call site, e.g.
// FLUXION_MEMORY_DOMAIN_ID_OF(Renderer). Hashes at runtime (not a
// compile-time constant) -- like FLUXION_TYPE_ID_OF, only safe to use
// inside a function (automatic storage), not as file-scope `static
// const` data.
#define FLUXION_MEMORY_DOMAIN_ID_OF(Name) Fluxion_MemoryDomainId_FromName(Fluxion_StringView_FromCStr(#Name))
