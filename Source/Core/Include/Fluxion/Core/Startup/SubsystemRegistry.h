#pragma once

#include <Fluxion/Core/Startup/SubsystemDesc.h>
#include <Fluxion/Core/Startup/SubsystemId.h>
#include <Fluxion/Foundation/Result.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

void Fluxion_SubsystemRegistry_Init(void);
void Fluxion_SubsystemRegistry_Shutdown(void);

// Copies *desc into the registry's fixed-capacity storage. Does not
// validate dependencies or start anything -- that happens once, for the
// whole registered set, in StartupAll. Returns false if the registry is
// full, the id is already registered, or dependencyCount exceeds
// FLUXION_SUBSYSTEM_MAX_DEPENDENCIES.
bool Fluxion_SubsystemRegistry_Register(const FluxionSubsystemDesc* desc);

// Validates every dependency resolves, detects cycles, computes a
// topological order, then starts every registered subsystem in that order.
// On the first startup() failure, already-started subsystems from this
// call are shut down in reverse order and the failure is returned --
// StartupAll never leaves a partially-started registry.
FluxionResult Fluxion_SubsystemRegistry_StartupAll(void);

// Shuts down every running subsystem in the exact reverse of the order it
// was started in.
void Fluxion_SubsystemRegistry_ShutdownAll(void);

bool Fluxion_SubsystemRegistry_IsRunning(FluxionSubsystemId id);

#ifdef __cplusplus
}
#endif
