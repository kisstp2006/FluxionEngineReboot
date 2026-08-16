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

#include <Fluxion/Core/Jobs/JobHandle.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bounds the job pool (JobSystem.c) -- zero heap allocation, same
// rationale as FLUXION_MAX_SUBSYSTEMS/FLUXION_MAX_SERVICES/
// FLUXION_MAX_MEMORY_DOMAINS.
#define FLUXION_MAX_INFLIGHT_JOBS 1024

// A job's captured data must fit here -- Submit() copies it in by value,
// no heap allocation. The C++ facade (Jobs.hpp) enforces this at compile
// time via static_assert(sizeof(F) <= FLUXION_JOB_INLINE_DATA_SIZE).
#define FLUXION_JOB_INLINE_DATA_SIZE 48

#define FLUXION_JOB_MAX_DEPENDENCIES 8

typedef void (*FluxionJobFn)(void* data);

// `dependencies` points at caller-owned storage (typically a local
// array) -- Fluxion_JobSystem_Submit copies its contents (up to
// FLUXION_JOB_MAX_DEPENDENCIES entries) into the job pool's own slot, so
// the caller's array only needs to outlive the Submit call itself, same
// convention as FluxionSubsystemDesc.
typedef struct FluxionJobDesc
{
    FluxionJobFn function;
    u8 data[FLUXION_JOB_INLINE_DATA_SIZE];
    usize dataSize;

    const FluxionJobHandle* dependencies;
    u32 dependencyCount;
} FluxionJobDesc;

#ifdef __cplusplus
}
#endif
