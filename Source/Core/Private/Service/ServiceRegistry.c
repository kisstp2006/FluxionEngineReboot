#include <Fluxion/Core/Service/ServiceRegistry.h>

#include <Fluxion/Foundation/Assert.h>

#define FLUXION_SERVICE_INDEX_NOT_FOUND ((usize)-1)

typedef struct FluxionRegisteredService
{
    FluxionServiceId id;
    u32 version;
    const void* interfacePointer;
} FluxionRegisteredService;

static FluxionRegisteredService s_services[FLUXION_MAX_SERVICES];
static usize s_serviceCount = 0;
static bool s_initialized = false;

void Fluxion_ServiceRegistry_Init(void)
{
    FLUXION_ASSERT_MSG(!s_initialized, "Fluxion_ServiceRegistry_Init called twice without a Shutdown in between");
    s_serviceCount = 0;
    s_initialized = true;
}

void Fluxion_ServiceRegistry_Shutdown(void)
{
    FLUXION_ASSERT_MSG(s_initialized, "Fluxion_ServiceRegistry_Shutdown called before Init");
    s_serviceCount = 0;
    s_initialized = false;
}

static usize Fluxion_FindServiceIndex(FluxionServiceId id)
{
    for (usize i = 0; i < s_serviceCount; ++i)
    {
        if (s_services[i].id == id)
        {
            return i;
        }
    }
    return FLUXION_SERVICE_INDEX_NOT_FOUND;
}

bool Fluxion_ServiceRegistry_Register(const void* interfacePointer)
{
    FLUXION_ASSERT(s_initialized);
    FLUXION_ASSERT(interfacePointer != NULL);

    const FluxionServiceHeader* header = (const FluxionServiceHeader*)interfacePointer;

    if (s_serviceCount >= FLUXION_MAX_SERVICES) return false;
    if (Fluxion_FindServiceIndex(header->serviceId) != FLUXION_SERVICE_INDEX_NOT_FOUND) return false;

    FluxionRegisteredService* slot = &s_services[s_serviceCount];
    slot->id = header->serviceId;
    slot->version = header->version;
    slot->interfacePointer = interfacePointer;

    ++s_serviceCount;
    return true;
}

void Fluxion_ServiceRegistry_Unregister(FluxionServiceId id)
{
    FLUXION_ASSERT(s_initialized);

    usize index = Fluxion_FindServiceIndex(id);
    if (index == FLUXION_SERVICE_INDEX_NOT_FOUND) return;

    // Swap-with-last removal -- registration order carries no meaning
    // here (unlike the Subsystem Registry's startup order), so this stays
    // O(1) instead of shifting the tail down.
    s_services[index] = s_services[s_serviceCount - 1];
    --s_serviceCount;
}

const void* Fluxion_ServiceRegistry_Get(FluxionServiceId id, u32 minVersion)
{
    FLUXION_ASSERT(s_initialized);

    usize index = Fluxion_FindServiceIndex(id);
    if (index == FLUXION_SERVICE_INDEX_NOT_FOUND) return NULL;
    if (s_services[index].version < minVersion) return NULL;

    return s_services[index].interfacePointer;
}
