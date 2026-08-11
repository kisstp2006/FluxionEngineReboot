#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// A coarse, informational grouping only -- actual startup order is derived
// entirely from each subsystem's declared dependencies (topological sort),
// never from phase value. A subsystem's phase is not validated against its
// dependencies' phases; it exists for humans skimming a subsystem list, not
// as a second ordering mechanism.
typedef enum FluxionStartupPhase
{
    FLUXION_STARTUP_PHASE_BOOTSTRAP = 0,
    FLUXION_STARTUP_PHASE_CORE,
    FLUXION_STARTUP_PHASE_PLATFORM,
    FLUXION_STARTUP_PHASE_APPLICATION,
    FLUXION_STARTUP_PHASE_RUNTIME,
    FLUXION_STARTUP_PHASE_TOOLS,
} FluxionStartupPhase;

#ifdef __cplusplus
}
#endif
