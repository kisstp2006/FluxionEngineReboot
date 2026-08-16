// The contents of this file are subject to the Common Public Attribution
// License Version 1.0 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// https://opensource.org/license/cpal-1-0. The License is based on the
// Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
// to cover use of software over a computer network and provide for limited
// attribution for the Original Developer. In addition, Exhibit A has been
// modified to be consistent with Exhibit B.
//
// Software distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
// for the specific language governing rights and limitations under the
// License.
//
// The Original Code is Fluxion Engine.
//
// The Original Developer is not the Initial Developer and is __________. If
// left blank, the Original Developer is the Initial Developer.
//
// The Initial Developer of the Original Code is Kiss Tibor Péter. All
// portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
// All Rights Reserved.
//
// Contributor ______________________.
//
// Alternatively, the contents of this file may be used under the terms of
// the Fluxion Engine Commercial License Agreement Version 1.0, separately
// obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
// License"), in which case the provisions of the Commercial License are
// applicable instead of those above.
//
// If you wish to allow use of your version of this file only under the terms
// of the Commercial License and not to allow others to use your version of
// this file under the CPAL, indicate your decision by deleting the
// provisions above and replace them with the notice and other provisions
// required by the Commercial License. If you do not delete the provisions
// above, a recipient may use your version of this file under either the CPAL
// or the Commercial License.
//
// SPDX-License-Identifier: CPAL-1.0

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
