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

#include <Fluxion/Foundation/Log.h>

void Test_Reflection_Run(TestContext* ctx);
void Test_Plugin_Run(TestContext* ctx);
void Test_Startup_Run(TestContext* ctx);
void Test_StartupCpp_Run(TestContext* ctx);
void Test_PluginSubsystem_Run(TestContext* ctx);
void Test_Service_Run(TestContext* ctx);
void Test_ServiceCpp_Run(TestContext* ctx);
void Test_ReflectionCpp_Run(TestContext* ctx);
void Test_Jobs_Run(TestContext* ctx);
void Test_JobsCpp_Run(TestContext* ctx);
void Test_BinarySerializer_Run(TestContext* ctx);
#if FLUXION_PROFILING
void Test_Profile_Run(TestContext* ctx);
void Test_ProfileCpp_Run(TestContext* ctx);
#endif

int main(void)
{
    TestContext ctx = { 0 };

    FLUXION_LOG_INFO("CoreTests", "Running CoreTests...");

    Test_Reflection_Run(&ctx);
    Test_Plugin_Run(&ctx);
    Test_Startup_Run(&ctx);
    Test_StartupCpp_Run(&ctx);
    Test_PluginSubsystem_Run(&ctx);
    Test_Service_Run(&ctx);
    Test_ServiceCpp_Run(&ctx);
    Test_ReflectionCpp_Run(&ctx);
    Test_Jobs_Run(&ctx);
    Test_JobsCpp_Run(&ctx);
    Test_BinarySerializer_Run(&ctx);
#if FLUXION_PROFILING
    Test_Profile_Run(&ctx);
    Test_ProfileCpp_Run(&ctx);
#endif

    if (ctx.failures == 0)
    {
        FLUXION_LOG_INFO("CoreTests", "All CoreTests passed.");
        return 0;
    }

    FLUXION_LOG_ERROR("CoreTests", "%d CoreTests check(s) failed.", ctx.failures);
    return 1;
}
