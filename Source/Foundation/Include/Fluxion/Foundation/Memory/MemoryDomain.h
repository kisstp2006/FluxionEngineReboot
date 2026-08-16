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

#include <Fluxion/Foundation/Containers/StringView.h>
#include <Fluxion/Foundation/StableId.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

FLUXION_DEFINE_STABLE_ID(FluxionMemoryDomainId, MemoryDomainId)

#define FLUXION_MEMORY_DOMAIN_ID_INVALID ((FluxionMemoryDomainId)0)

// Bounds the tracker's fixed-capacity storage (MemoryTracker.c) -- zero
// heap allocation, same rationale as FLUXION_MAX_SUBSYSTEMS/
// FLUXION_MAX_SERVICES.
#define FLUXION_MAX_MEMORY_DOMAINS 64

// A domain is one node in a tree (parent = FLUXION_MEMORY_DOMAIN_ID_INVALID
// for a root domain) -- statistics roll up from children into parents.
// Both a broad category (Foundation, Core, Renderer, ...) and a
// finer-grained label under it are just nodes in this same tree, not two
// parallel hierarchies.
typedef struct FluxionMemoryDomainDesc
{
    FluxionMemoryDomainId id;
    const char* name;
    FluxionMemoryDomainId parent;
} FluxionMemoryDomainDesc;

#ifdef __cplusplus
}
#endif

// Computes a FluxionMemoryDomainId from a name at the call site, e.g.
// FLUXION_MEMORY_DOMAIN_ID_OF(Renderer). Hashes at runtime (not a
// compile-time constant) -- like FLUXION_TYPE_ID_OF, only safe to use
// inside a function (automatic storage), not as file-scope `static
// const` data.
#define FLUXION_MEMORY_DOMAIN_ID_OF(Name) Fluxion_MemoryDomainId_FromName(Fluxion_StringView_FromCStr(#Name))
