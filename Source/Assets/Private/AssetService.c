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

#include <Fluxion/Assets/AssetService.h>

#include <Fluxion/Assets/VirtualFileSystem.h>
#include <Fluxion/Core/Service/ServiceRegistry.h>

static FluxionAssetService s_service;
static bool s_registered = false;

FluxionServiceId Fluxion_AssetService_Id(void)
{
    return FLUXION_SERVICE_ID_OF(Assets);
}

bool Fluxion_AssetService_Register(void)
{
    if (s_registered) return true;

    s_service.header.serviceId = Fluxion_AssetService_Id();
    s_service.header.version = FLUXION_ASSET_SERVICE_VERSION;
    s_service.header.structSize = (u32)sizeof(FluxionAssetService);

    s_service.registerType = Fluxion_AssetTypes_Register;
    s_service.unregisterType = Fluxion_AssetTypes_Unregister;
    s_service.addAsset = Fluxion_AssetDatabase_Add;
    s_service.acquire = Fluxion_Assets_Acquire;
    s_service.release = Fluxion_Assets_Release;
    s_service.getState = Fluxion_Assets_GetState;
    s_service.getObject = Fluxion_Assets_GetObject;
    s_service.readFile = Fluxion_Vfs_ReadAll;
    s_service.freeBuffer = Fluxion_Vfs_FreeBuffer;

    s_registered = Fluxion_ServiceRegistry_Register(&s_service);
    return s_registered;
}

void Fluxion_AssetService_Unregister(void)
{
    if (!s_registered) return;

    Fluxion_ServiceRegistry_Unregister(Fluxion_AssetService_Id());
    s_registered = false;
}
