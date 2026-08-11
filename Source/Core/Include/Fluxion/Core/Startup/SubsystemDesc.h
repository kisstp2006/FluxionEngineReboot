#pragma once

#include <Fluxion/Core/Startup/StartupPhase.h>
#include <Fluxion/Core/Startup/SubsystemId.h>
#include <Fluxion/Foundation/Result.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bounds the registry's fixed-capacity storage (SubsystemRegistry.c) --
// generous for an engine-lifetime set of subsystems, small enough that
// zero heap allocation and a linear scan both stay cheap.
#define FLUXION_MAX_SUBSYSTEMS 128
#define FLUXION_SUBSYSTEM_MAX_DEPENDENCIES 8

// Fully POD. `dependencies` points at caller-owned storage (typically a
// file-scope `static const FluxionSubsystemId[]`) -- the registry copies
// the array contents (up to FLUXION_SUBSYSTEM_MAX_DEPENDENCIES entries)
// into its own descriptor slot on Register, so the caller's array only
// needs to outlive the Register call itself.
typedef struct FluxionSubsystemDesc
{
    FluxionSubsystemId id;
    const char* name;
    FluxionStartupPhase phase;

    const FluxionSubsystemId* dependencies;
    u32 dependencyCount;

    FluxionResult (*startup)(void* userdata);
    void (*shutdown)(void* userdata);
    void* userdata;
} FluxionSubsystemDesc;

#ifdef __cplusplus
}
#endif
