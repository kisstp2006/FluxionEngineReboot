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

#pragma once

#include <Fluxion/Core/Diagnostics/ProfileBackend.h>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Diagnostics/SourceLocation.h>

#ifdef __cplusplus
extern "C" {
#endif

#if FLUXION_PROFILING

// backend must stay valid until ClearBackend() is called or a different
// backend replaces it -- the profiler stores the pointer, it does not
// copy the pointed-to table. Call during startup/shutdown, not
// concurrently with other threads already emitting zones/markers.
void Fluxion_Profiler_SetBackend(const FluxionProfileBackend* backend);
void Fluxion_Profiler_ClearBackend(void);

// No-ops (near-zero cost, single null check) when no backend is attached.
// There is no internal nesting stack -- the hierarchy a backend sees is
// exactly the caller's Begin/End call order, so mismatched Begin/End
// pairs are the caller's responsibility (ProfileScope.hpp's RAII pairing
// is the intended way to always get this right).
void Fluxion_Profiler_ZoneBegin(const FluxionSourceLocation* location, const char* name);
void Fluxion_Profiler_ZoneEnd(void);
void Fluxion_Profiler_Marker(const FluxionSourceLocation* location, const char* name);

// Renames the calling OS thread (via Fluxion_Platform_SetCurrentThreadName)
// and, if a backend is attached, also reports the name to it -- one call
// instead of two so callers never rename the OS thread while forgetting
// to tell the profiler backend, or vice versa.
void Fluxion_Profiler_SetThreadName(const char* name);

#else

// FLUXION_PROFILING is off: every call below compiles to nothing -- a
// compile-time no-op, not a runtime branch, matching MemoryTracker.h's
// zero-cost-when-disabled approach.
static inline void Fluxion_Profiler_SetBackend(const FluxionProfileBackend* backend) { FLUXION_UNUSED(backend); }
static inline void Fluxion_Profiler_ClearBackend(void) {}

static inline void Fluxion_Profiler_ZoneBegin(const FluxionSourceLocation* location, const char* name)
{
    FLUXION_UNUSED(location);
    FLUXION_UNUSED(name);
}

static inline void Fluxion_Profiler_ZoneEnd(void) {}

static inline void Fluxion_Profiler_Marker(const FluxionSourceLocation* location, const char* name)
{
    FLUXION_UNUSED(location);
    FLUXION_UNUSED(name);
}

// Still renames the OS thread even when profiling is compiled out --
// thread naming is useful in a debugger regardless of whether a profiler
// backend is attached, and Fluxion_Platform_SetCurrentThreadName has no
// meaningful cost to skip.
void Fluxion_Profiler_SetThreadName(const char* name);

#endif

#ifdef __cplusplus
}
#endif
