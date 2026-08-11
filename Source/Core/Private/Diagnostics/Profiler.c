#include <Fluxion/Core/Diagnostics/Profiler.h>

#include <Fluxion/Foundation/Memory/MemoryTracker.h>
#include <Fluxion/Platform/Thread.h>

#include <stdio.h>

#if FLUXION_PROFILING

// Attach/detach is a startup/shutdown-shaped operation (hook up a
// profiler once, tear it down once), not something that races with
// worker threads emitting zones -- same unsynchronized-global shape as
// the Service/Subsystem registries and MemoryTracker's domain table.
static const FluxionProfileBackend* s_backend = NULL;

// Bridges MemoryTracker's (Foundation) push/pop hook into a marker on
// whatever backend is currently attached here -- Foundation can't depend
// on Core's Profiler, so this wiring lives on this side, connected only
// while a backend is actually attached (SetBackend/ClearBackend below).
static void Fluxion_Profiler_OnMemoryDomainEvent(FluxionMemoryDomainId id, bool isPush, const FluxionSourceLocation* location, void* userData)
{
    FLUXION_UNUSED(userData);

    const char* domainName = Fluxion_MemoryTracker_GetDomainName(id);
    if (domainName == NULL) domainName = "?";

    char text[64];
    snprintf(text, sizeof(text), "MemoryDomain %s: %s", domainName, isPush ? "push" : "pop");

    FluxionSourceLocation fallback = { NULL, NULL, 0 };
    Fluxion_Profiler_Marker(location != NULL ? location : &fallback, text);
}

void Fluxion_Profiler_SetBackend(const FluxionProfileBackend* backend)
{
    s_backend = backend;
    Fluxion_MemoryTracker_SetHook(Fluxion_Profiler_OnMemoryDomainEvent, NULL);
}

void Fluxion_Profiler_ClearBackend(void)
{
    s_backend = NULL;
    Fluxion_MemoryTracker_ClearHook();
}

void Fluxion_Profiler_ZoneBegin(const FluxionSourceLocation* location, const char* name)
{
    if (s_backend == NULL || s_backend->zoneBegin == NULL) return;
    s_backend->zoneBegin(location, name, s_backend->userData);
}

void Fluxion_Profiler_ZoneEnd(void)
{
    if (s_backend == NULL || s_backend->zoneEnd == NULL) return;
    s_backend->zoneEnd(s_backend->userData);
}

void Fluxion_Profiler_Marker(const FluxionSourceLocation* location, const char* name)
{
    if (s_backend == NULL || s_backend->marker == NULL) return;
    s_backend->marker(location, name, s_backend->userData);
}

void Fluxion_Profiler_SetThreadName(const char* name)
{
    Fluxion_Platform_SetCurrentThreadName(name);
    if (s_backend == NULL || s_backend->threadName == NULL) return;
    s_backend->threadName(name, s_backend->userData);
}

#else

void Fluxion_Profiler_SetThreadName(const char* name)
{
    Fluxion_Platform_SetCurrentThreadName(name);
}

#endif
