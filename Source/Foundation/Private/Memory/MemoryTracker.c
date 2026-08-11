#include <Fluxion/Foundation/Memory/MemoryTracker.h>

#if FLUXION_MEMORY_TRACKING

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Log.h>

#define FLUXION_MEMORY_DOMAIN_INDEX_NOT_FOUND ((usize)-1)

typedef struct FluxionTrackedDomain
{
    FluxionMemoryDomainId id;
    const char* name;
    FluxionMemoryDomainId parent;
    FluxionMemoryStatistics stats;
} FluxionTrackedDomain;

static FluxionTrackedDomain s_domains[FLUXION_MAX_MEMORY_DOMAINS];
static usize s_domainCount = 0;
static bool s_initialized = false;

// Registration/attach-shaped state, same reasoning as the Service/
// Subsystem Registry and the Profiler's backend slot -- set once during
// startup, not written concurrently with threads already pushing/popping
// domains.
static FluxionMemoryTrackerHookFn s_hook = NULL;
static void* s_hookUserData = NULL;

// Thread-local: each thread has its own scope stack, since two threads
// allocating "at the same time" are conceptually in different scopes.
static FLUXION_THREAD_LOCAL FluxionMemoryDomainId s_domainStack[FLUXION_MAX_MEMORY_DOMAINS];
static FLUXION_THREAD_LOCAL FluxionSourceLocation s_domainLocationStack[FLUXION_MAX_MEMORY_DOMAINS];
static FLUXION_THREAD_LOCAL usize s_domainStackCount = 0;

void Fluxion_MemoryTracker_Init(void)
{
    FLUXION_ASSERT_MSG(!s_initialized, "Fluxion_MemoryTracker_Init called twice without a Shutdown in between");
    s_domainCount = 0;
    s_initialized = true;
}

void Fluxion_MemoryTracker_Shutdown(void)
{
    FLUXION_ASSERT_MSG(s_initialized, "Fluxion_MemoryTracker_Shutdown called before Init");

    for (usize i = 0; i < s_domainCount; ++i)
    {
        const FluxionTrackedDomain* domain = &s_domains[i];
        if (domain->stats.allocationCount != domain->stats.deallocationCount)
        {
            FLUXION_LOG_WARN("Memory", "Domain '%s' has %zu unmatched allocation(s) (%zu bytes) at shutdown",
                domain->name,
                domain->stats.allocationCount - domain->stats.deallocationCount,
                domain->stats.currentBytes);
        }
    }

    s_domainCount = 0;
    s_initialized = false;
}

static usize Fluxion_FindMemoryDomainIndex(FluxionMemoryDomainId id)
{
    for (usize i = 0; i < s_domainCount; ++i)
    {
        if (s_domains[i].id == id)
        {
            return i;
        }
    }
    return FLUXION_MEMORY_DOMAIN_INDEX_NOT_FOUND;
}

bool Fluxion_MemoryTracker_RegisterDomain(const FluxionMemoryDomainDesc* desc)
{
    FLUXION_ASSERT(s_initialized);

    if (s_domainCount >= FLUXION_MAX_MEMORY_DOMAINS) return false;
    if (Fluxion_FindMemoryDomainIndex(desc->id) != FLUXION_MEMORY_DOMAIN_INDEX_NOT_FOUND) return false;
    if (desc->parent != FLUXION_MEMORY_DOMAIN_ID_INVALID &&
        Fluxion_FindMemoryDomainIndex(desc->parent) == FLUXION_MEMORY_DOMAIN_INDEX_NOT_FOUND)
    {
        return false; // parent must already be registered
    }

    FluxionTrackedDomain* domain = &s_domains[s_domainCount];
    domain->id = desc->id;
    domain->name = desc->name;
    domain->parent = desc->parent;
    domain->stats.allocationCount = 0;
    domain->stats.deallocationCount = 0;
    domain->stats.currentBytes = 0;
    domain->stats.peakBytes = 0;

    ++s_domainCount;
    return true;
}

const char* Fluxion_MemoryTracker_GetDomainName(FluxionMemoryDomainId id)
{
    usize index = Fluxion_FindMemoryDomainIndex(id);
    if (index == FLUXION_MEMORY_DOMAIN_INDEX_NOT_FOUND) return NULL;
    return s_domains[index].name;
}

void Fluxion_MemoryTracker_PushDomain(FluxionMemoryDomainId id, FluxionSourceLocation location)
{
    FLUXION_ASSERT(s_initialized);
    FLUXION_ASSERT_MSG(s_domainStackCount < FLUXION_MAX_MEMORY_DOMAINS, "memory domain scope stack overflow");
    s_domainStack[s_domainStackCount] = id;
    s_domainLocationStack[s_domainStackCount] = location;
    ++s_domainStackCount;

    if (s_hook != NULL)
    {
        s_hook(id, true, &location, s_hookUserData);
    }
}

void Fluxion_MemoryTracker_PopDomain(void)
{
    FLUXION_ASSERT_MSG(s_domainStackCount > 0, "Fluxion_MemoryTracker_PopDomain called with an empty scope stack");
    --s_domainStackCount;

    if (s_hook != NULL)
    {
        s_hook(s_domainStack[s_domainStackCount], false, NULL, s_hookUserData);
    }
}

FluxionMemoryDomainId Fluxion_MemoryTracker_GetCurrentDomain(void)
{
    if (s_domainStackCount == 0) return FLUXION_MEMORY_DOMAIN_ID_INVALID;
    return s_domainStack[s_domainStackCount - 1];
}

FluxionSourceLocation Fluxion_MemoryTracker_GetCurrentLocation(void)
{
    if (s_domainStackCount == 0)
    {
        FluxionSourceLocation empty = { NULL, NULL, 0 };
        return empty;
    }
    return s_domainLocationStack[s_domainStackCount - 1];
}

FluxionMemoryStatistics Fluxion_MemoryTracker_GetStatistics(FluxionMemoryDomainId id)
{
    usize index = Fluxion_FindMemoryDomainIndex(id);
    if (index == FLUXION_MEMORY_DOMAIN_INDEX_NOT_FOUND)
    {
        FluxionMemoryStatistics empty = { 0, 0, 0, 0 };
        return empty;
    }
    return s_domains[index].stats;
}

// Both record functions walk the parent chain so a child domain's
// allocations roll up into every ancestor's totals too (Foundation's
// stats include everything Core/Renderer/... allocate under it).
void Fluxion_MemoryTracker_RecordAlloc(FluxionMemoryDomainId id, usize size)
{
    FLUXION_ASSERT(s_initialized);

    FluxionMemoryDomainId current = id;
    while (current != FLUXION_MEMORY_DOMAIN_ID_INVALID)
    {
        usize index = Fluxion_FindMemoryDomainIndex(current);
        if (index == FLUXION_MEMORY_DOMAIN_INDEX_NOT_FOUND) break;

        FluxionMemoryStatistics* stats = &s_domains[index].stats;
        ++stats->allocationCount;
        stats->currentBytes += size;
        if (stats->currentBytes > stats->peakBytes)
        {
            stats->peakBytes = stats->currentBytes;
        }

        current = s_domains[index].parent;
    }
}

void Fluxion_MemoryTracker_RecordFree(FluxionMemoryDomainId id, usize size)
{
    FLUXION_ASSERT(s_initialized);

    FluxionMemoryDomainId current = id;
    while (current != FLUXION_MEMORY_DOMAIN_ID_INVALID)
    {
        usize index = Fluxion_FindMemoryDomainIndex(current);
        if (index == FLUXION_MEMORY_DOMAIN_INDEX_NOT_FOUND) break;

        FluxionMemoryStatistics* stats = &s_domains[index].stats;
        ++stats->deallocationCount;
        FLUXION_ASSERT_MSG(stats->currentBytes >= size, "freed more bytes than were recorded as allocated for this domain");
        stats->currentBytes -= size;

        current = s_domains[index].parent;
    }
}

void Fluxion_MemoryTracker_SetHook(FluxionMemoryTrackerHookFn hook, void* userData)
{
    s_hook = hook;
    s_hookUserData = userData;
}

void Fluxion_MemoryTracker_ClearHook(void)
{
    s_hook = NULL;
    s_hookUserData = NULL;
}

#endif // FLUXION_MEMORY_TRACKING
