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

#include <Fluxion/Assets/AssetSystem.h>

#include <Fluxion/Assets/AssetDatabase.h>
#include <Fluxion/Assets/AssetService.h>
#include <Fluxion/Assets/AssetType.h>
#include <Fluxion/Assets/Assets.h>
#include <Fluxion/Assets/VirtualFileSystem.h>
#include <Fluxion/Foundation/Log.h>

#define FLUXION_ASSET_SYSTEM_LOG_CATEGORY "Assets"

static bool s_initialized = false;

bool Fluxion_AssetSystem_Init(FluxionAllocator* allocator)
{
    if (s_initialized) return true;

    Fluxion_Vfs_Init(allocator);
    Fluxion_AssetTypes_Init(allocator);
    Fluxion_AssetDatabase_Init(allocator);
    Fluxion_Assets_Init(allocator);

    if (!Fluxion_AssetService_Register())
    {
        FLUXION_LOG_ERROR(FLUXION_ASSET_SYSTEM_LOG_CATEGORY, "could not publish the asset service; no plugin would be able to add a type");

        Fluxion_Assets_Shutdown();
        Fluxion_AssetDatabase_Shutdown();
        Fluxion_AssetTypes_Shutdown();
        Fluxion_Vfs_Shutdown();
        return false;
    }

    s_initialized = true;
    return true;
}

void Fluxion_AssetSystem_Shutdown(void)
{
    if (!s_initialized) return;

    // The service goes first: it hands out function pointers into
    // everything below, and it must stop doing that before any of them
    // stops being there.
    Fluxion_AssetService_Unregister();

    Fluxion_Assets_Shutdown();
    Fluxion_AssetDatabase_Shutdown();
    Fluxion_AssetTypes_Shutdown();
    Fluxion_Vfs_Shutdown();

    s_initialized = false;
}

bool Fluxion_AssetSystem_IsInitialized(void)
{
    return s_initialized;
}
