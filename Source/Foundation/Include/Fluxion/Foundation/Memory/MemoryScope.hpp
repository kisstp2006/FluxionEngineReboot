#pragma once

#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Diagnostics/SourceLocation.h>
#include <Fluxion/Foundation/Memory/MemoryDomain.h>
#include <Fluxion/Foundation/Memory/MemoryTracker.h>

namespace Fluxion::Foundation
{

// RAII scope guard: pushes `domain` onto the memory tracker's scope
// stack on construction, pops it on destruction (ScopeExit.hpp's
// pattern). Always constructed as a named local via FLUXION_MEMORY_SCOPE,
// never returned from a function, so unlike ScopeExit it doesn't need to
// be movable -- copy and move are both deleted to rule out accidental
// double-push/pop.
class MemoryScope
{
public:
    MemoryScope(FluxionMemoryDomainId domain, const char* file, u32 line, const char* function)
    {
        FluxionSourceLocation location{ file, function, line };
        Fluxion_MemoryTracker_PushDomain(domain, location);
    }

    ~MemoryScope()
    {
        Fluxion_MemoryTracker_PopDomain();
    }

    MemoryScope(const MemoryScope&) = delete;
    MemoryScope& operator=(const MemoryScope&) = delete;
    MemoryScope(MemoryScope&&) = delete;
    MemoryScope& operator=(MemoryScope&&) = delete;
};

} // namespace Fluxion::Foundation

#define FLUXION_MEMORY_SCOPE(domainId) \
    ::Fluxion::Foundation::MemoryScope FLUXION_CONCAT(fluxionMemoryScope_, __LINE__)(domainId, __FILE__, __LINE__, __func__)
