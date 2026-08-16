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
static bool s_serviceRegistryInitialized = false;

void Fluxion_ServiceRegistry_Init(void)
{
    FLUXION_ASSERT_MSG(!s_serviceRegistryInitialized, "Fluxion_ServiceRegistry_Init called twice without a Shutdown in between");
    s_serviceCount = 0;
    s_serviceRegistryInitialized = true;
}

void Fluxion_ServiceRegistry_Shutdown(void)
{
    FLUXION_ASSERT_MSG(s_serviceRegistryInitialized, "Fluxion_ServiceRegistry_Shutdown called before Init");
    s_serviceCount = 0;
    s_serviceRegistryInitialized = false;
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
    FLUXION_ASSERT(s_serviceRegistryInitialized);
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
    FLUXION_ASSERT(s_serviceRegistryInitialized);

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
    FLUXION_ASSERT(s_serviceRegistryInitialized);

    usize index = Fluxion_FindServiceIndex(id);
    if (index == FLUXION_SERVICE_INDEX_NOT_FOUND) return NULL;
    if (s_services[index].version < minVersion) return NULL;

    return s_services[index].interfacePointer;
}
