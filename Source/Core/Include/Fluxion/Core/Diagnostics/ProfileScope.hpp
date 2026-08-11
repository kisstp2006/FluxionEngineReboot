#pragma once

#include <Fluxion/Core/Diagnostics/Profiler.h>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Diagnostics/SourceLocation.h>

namespace Fluxion::Core
{

// RAII scope guard: begins a profiler zone on construction, ends it on
// destruction (MemoryScope.hpp's pattern). Always constructed as a named
// local via FLUXION_PROFILE_SCOPE/FLUXION_PROFILE_FUNCTION, never
// returned from a function, so it doesn't need to be movable -- copy and
// move are both deleted to rule out accidental double-begin/end.
class ProfileScope
{
public:
    ProfileScope(const char* name, const char* file, u32 line, const char* function)
        : m_location{ file, function, line }
    {
        Fluxion_Profiler_ZoneBegin(&m_location, name);
    }

    ~ProfileScope()
    {
        Fluxion_Profiler_ZoneEnd();
    }

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;
    ProfileScope(ProfileScope&&) = delete;
    ProfileScope& operator=(ProfileScope&&) = delete;

private:
    FluxionSourceLocation m_location;
};

} // namespace Fluxion::Core

#define FLUXION_PROFILE_SCOPE(name) \
    ::Fluxion::Core::ProfileScope FLUXION_CONCAT(fluxionProfileScope_, __LINE__)(name, __FILE__, __LINE__, __func__)

#define FLUXION_PROFILE_FUNCTION() FLUXION_PROFILE_SCOPE(__func__)
