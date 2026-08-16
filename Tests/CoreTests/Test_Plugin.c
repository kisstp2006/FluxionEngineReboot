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

#include "TestFramework.h"

#include <Fluxion/Core/Plugin/Manager.h>

#include <string.h>

void Test_Plugin_Run(TestContext* ctx)
{
    // Basic load + dependency ordering.
    Fluxion_PluginManager_Init(NULL);
    {
        const char* paths[] = { FLUXION_TEST_HELLO_PLUGIN_PATH, FLUXION_TEST_HELLO_PLUGIN_DEPENDENT_PATH };
        bool loaded = Fluxion_PluginManager_LoadAll(paths, 2);
        TEST_CHECK(ctx, loaded);

        if (loaded)
        {
            TEST_CHECK(ctx, Fluxion_PluginManager_GetLoadedCount() == 2);
            TEST_CHECK(ctx, strcmp(Fluxion_PluginManager_GetLoadedDescriptor(0)->name, "HelloPlugin") == 0);
            TEST_CHECK(ctx, strcmp(Fluxion_PluginManager_GetLoadedDescriptor(1)->name, "HelloPluginDependent") == 0);
            TEST_CHECK(ctx, Fluxion_PluginManager_GetLoadedApi(0)->userData == (void*)1);
            TEST_CHECK(ctx, Fluxion_PluginManager_GetLoadedApi(1)->userData == (void*)2);
        }
    }
    Fluxion_PluginManager_Shutdown();

    // Paths given in reverse dependency order still resolve correctly —
    // load order comes from the declared dependencies, not array order.
    Fluxion_PluginManager_Init(NULL);
    {
        const char* paths[] = { FLUXION_TEST_HELLO_PLUGIN_DEPENDENT_PATH, FLUXION_TEST_HELLO_PLUGIN_PATH };
        bool loaded = Fluxion_PluginManager_LoadAll(paths, 2);
        TEST_CHECK(ctx, loaded);

        if (loaded)
        {
            TEST_CHECK(ctx, strcmp(Fluxion_PluginManager_GetLoadedDescriptor(0)->name, "HelloPlugin") == 0);
            TEST_CHECK(ctx, strcmp(Fluxion_PluginManager_GetLoadedDescriptor(1)->name, "HelloPluginDependent") == 0);
        }
    }
    Fluxion_PluginManager_Shutdown();

    // Circular dependency: LoadAll must fail and load nothing.
    Fluxion_PluginManager_Init(NULL);
    {
        const char* paths[] = { FLUXION_TEST_CYCLIC_PLUGIN_A_PATH, FLUXION_TEST_CYCLIC_PLUGIN_B_PATH };
        TEST_CHECK(ctx, Fluxion_PluginManager_LoadAll(paths, 2) == false);
        TEST_CHECK(ctx, Fluxion_PluginManager_GetLoadedCount() == 0);
    }
    Fluxion_PluginManager_Shutdown();

    // Malformed .plugin JSON: LoadAll must fail and load nothing.
    Fluxion_PluginManager_Init(NULL);
    {
        const char* paths[] = { FLUXION_TEST_BAD_PLUGIN_PATH };
        TEST_CHECK(ctx, Fluxion_PluginManager_LoadAll(paths, 1) == false);
        TEST_CHECK(ctx, Fluxion_PluginManager_GetLoadedCount() == 0);
    }
    Fluxion_PluginManager_Shutdown();
}
