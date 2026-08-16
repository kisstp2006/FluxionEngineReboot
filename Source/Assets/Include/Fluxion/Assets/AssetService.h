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

#include <Fluxion/Assets/AssetDatabase.h>
#include <Fluxion/Assets/AssetType.h>
#include <Fluxion/Assets/Assets.h>
#include <Fluxion/Core/Service/ServiceHeader.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

// How a plugin adds an asset type.
//
// THROUGH THE SERVICE REGISTRY, NOT THROUGH THE PLUGIN INTERFACE. A
// plugin already gets a way to ask the host for a service by name; using
// it here means that adding a new kind of asset does not raise the host
// interface's version number. Raising that number makes every plugin
// built against the old one out of date at once, and "someone wrote an
// importer" is not a reason for that to happen.
//
// A PLUGIN THAT REGISTERS A TYPE MUST UNREGISTER IT BEFORE IT UNLOADS.
// The descriptor is copied, so its bytes are safe; the functions in it
// are not. They live in the plugin's own library, and unloading that
// library leaves them pointing at nothing that will say so.

#define FLUXION_ASSET_SERVICE_VERSION 1

typedef struct FluxionAssetService
{
    FluxionServiceHeader header;

    bool (*registerType)(const FluxionAssetTypeDesc* desc);
    bool (*unregisterType)(FluxionAssetTypeId id);

    bool (*addAsset)(const FluxionAssetDesc* desc, FluxionUUID* outId);

    FluxionAssetHandle (*acquire)(FluxionUUID id);
    void (*release)(FluxionAssetHandle handle);
    FluxionAssetState (*getState)(FluxionAssetHandle handle);
    void* (*getObject)(FluxionAssetHandle handle);

    // An importer reads its source file through the mount layer like
    // everything else, rather than opening a path -- so an importer
    // written today keeps working where paths do not exist.
    u8* (*readFile)(const char* path, usize* outSize);
    void (*freeBuffer)(u8* buffer, usize size);
} FluxionAssetService;

FluxionServiceId Fluxion_AssetService_Id(void);

bool Fluxion_AssetService_Register(void);
void Fluxion_AssetService_Unregister(void);

#ifdef __cplusplus
}
#endif
