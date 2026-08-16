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
