#include <Fluxion/Core/Startup/SubsystemRegistry.h>

#include <Fluxion/Foundation/Assert.h>

#include <string.h>

#define FLUXION_SUBSYSTEM_INDEX_NOT_FOUND ((usize)-1)

enum
{
    FLUXION_SUBSYSTEM_STARTUP_ERROR_MISSING_DEPENDENCY = 1,
    FLUXION_SUBSYSTEM_STARTUP_ERROR_CIRCULAR_DEPENDENCY = 2,
};

typedef struct FluxionRegisteredSubsystem
{
    FluxionSubsystemDesc desc;
    FluxionSubsystemId dependenciesStorage[FLUXION_SUBSYSTEM_MAX_DEPENDENCIES];
} FluxionRegisteredSubsystem;

static FluxionRegisteredSubsystem s_subsystems[FLUXION_MAX_SUBSYSTEMS];
static usize s_subsystemCount = 0;
static bool s_subsystemRegistryInitialized = false;

// Indices into s_subsystems, in the order they were actually started --
// ShutdownAll (and StartupAll's own rollback) walk this in reverse.
static usize s_startupOrder[FLUXION_MAX_SUBSYSTEMS];
static usize s_startedCount = 0;

void Fluxion_SubsystemRegistry_Init(void)
{
    FLUXION_ASSERT_MSG(!s_subsystemRegistryInitialized, "Fluxion_SubsystemRegistry_Init called twice without a Shutdown in between");
    s_subsystemCount = 0;
    s_startedCount = 0;
    s_subsystemRegistryInitialized = true;
}

void Fluxion_SubsystemRegistry_Shutdown(void)
{
    FLUXION_ASSERT_MSG(s_subsystemRegistryInitialized, "Fluxion_SubsystemRegistry_Shutdown called before Init");
    if (s_startedCount > 0)
    {
        Fluxion_SubsystemRegistry_ShutdownAll();
    }
    s_subsystemCount = 0;
    s_subsystemRegistryInitialized = false;
}

static usize Fluxion_FindSubsystemIndex(FluxionSubsystemId id)
{
    for (usize i = 0; i < s_subsystemCount; ++i)
    {
        if (s_subsystems[i].desc.id == id)
        {
            return i;
        }
    }
    return FLUXION_SUBSYSTEM_INDEX_NOT_FOUND;
}

bool Fluxion_SubsystemRegistry_Register(const FluxionSubsystemDesc* desc)
{
    FLUXION_ASSERT(s_subsystemRegistryInitialized);

    if (s_subsystemCount >= FLUXION_MAX_SUBSYSTEMS) return false;
    if (desc->dependencyCount > FLUXION_SUBSYSTEM_MAX_DEPENDENCIES) return false;
    if (Fluxion_FindSubsystemIndex(desc->id) != FLUXION_SUBSYSTEM_INDEX_NOT_FOUND) return false;

    FluxionRegisteredSubsystem* slot = &s_subsystems[s_subsystemCount];
    slot->desc = *desc;
    for (u32 d = 0; d < desc->dependencyCount; ++d)
    {
        slot->dependenciesStorage[d] = desc->dependencies[d];
    }
    slot->desc.dependencies = slot->dependenciesStorage;

    ++s_subsystemCount;
    return true;
}

FluxionResult Fluxion_SubsystemRegistry_StartupAll(void)
{
    FLUXION_ASSERT(s_subsystemRegistryInitialized);
    FLUXION_ASSERT_MSG(s_startedCount == 0, "Fluxion_SubsystemRegistry_StartupAll called while subsystems are already running");

    for (usize i = 0; i < s_subsystemCount; ++i)
    {
        const FluxionSubsystemDesc* desc = &s_subsystems[i].desc;
        for (u32 d = 0; d < desc->dependencyCount; ++d)
        {
            if (Fluxion_FindSubsystemIndex(desc->dependencies[d]) == FLUXION_SUBSYSTEM_INDEX_NOT_FOUND)
            {
                return Fluxion_ResultError(FLUXION_SUBSYSTEM_STARTUP_ERROR_MISSING_DEPENDENCY,
                    "subsystem depends on an unregistered subsystem");
            }
        }
    }

    // Topological sort (O(N^2) repeated-scan variant -- N is always small
    // and this runs once at startup, same trade-off already made by
    // Fluxion_PluginManager_LoadAll for plugin dependency resolution). A
    // full pass with no progress means a cycle, so this also doubles as
    // cycle detection without a separate DFS/color-marking pass.
    bool resolved[FLUXION_MAX_SUBSYSTEMS];
    memset(resolved, 0, sizeof(bool) * s_subsystemCount);
    usize order[FLUXION_MAX_SUBSYSTEMS];
    usize orderCount = 0;

    while (orderCount < s_subsystemCount)
    {
        usize progressBefore = orderCount;

        for (usize i = 0; i < s_subsystemCount; ++i)
        {
            if (resolved[i]) continue;

            const FluxionSubsystemDesc* desc = &s_subsystems[i].desc;
            bool ready = true;
            for (u32 d = 0; d < desc->dependencyCount; ++d)
            {
                usize depIndex = Fluxion_FindSubsystemIndex(desc->dependencies[d]);
                FLUXION_ASSERT_MSG(depIndex != FLUXION_SUBSYSTEM_INDEX_NOT_FOUND, "dependency should already be validated above");
                if (!resolved[depIndex])
                {
                    ready = false;
                    break;
                }
            }

            if (ready)
            {
                resolved[i] = true;
                order[orderCount++] = i;
            }
        }

        if (orderCount == progressBefore)
        {
            return Fluxion_ResultError(FLUXION_SUBSYSTEM_STARTUP_ERROR_CIRCULAR_DEPENDENCY,
                "circular subsystem dependency detected");
        }
    }

    // Start in dependency order. On the first failure, unwind everything
    // this call started so far (reverse order) -- StartupAll never leaves
    // a partially-started registry.
    for (usize k = 0; k < orderCount; ++k)
    {
        usize i = order[k];
        FluxionSubsystemDesc* desc = &s_subsystems[i].desc;

        FluxionResult result = desc->startup ? desc->startup(desc->userdata) : Fluxion_ResultOk();
        if (!result.ok)
        {
            for (usize r = s_startedCount; r > 0; --r)
            {
                usize rollbackIndex = s_startupOrder[r - 1];
                FluxionSubsystemDesc* rollbackDesc = &s_subsystems[rollbackIndex].desc;
                if (rollbackDesc->shutdown)
                {
                    rollbackDesc->shutdown(rollbackDesc->userdata);
                }
            }
            s_startedCount = 0;
            return result;
        }

        s_startupOrder[s_startedCount++] = i;
    }

    return Fluxion_ResultOk();
}

void Fluxion_SubsystemRegistry_ShutdownAll(void)
{
    FLUXION_ASSERT(s_subsystemRegistryInitialized);

    for (usize r = s_startedCount; r > 0; --r)
    {
        usize i = s_startupOrder[r - 1];
        FluxionSubsystemDesc* desc = &s_subsystems[i].desc;
        if (desc->shutdown)
        {
            desc->shutdown(desc->userdata);
        }
    }
    s_startedCount = 0;
}

bool Fluxion_SubsystemRegistry_IsRunning(FluxionSubsystemId id)
{
    for (usize r = 0; r < s_startedCount; ++r)
    {
        if (s_subsystems[s_startupOrder[r]].desc.id == id)
        {
            return true;
        }
    }
    return false;
}
