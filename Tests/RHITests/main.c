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

void Test_BackendAvailability_Run(TestContext* ctx);
void Test_Handles_Run(TestContext* ctx);
void Test_Format_Run(TestContext* ctx);
void Test_CompressedUpload_Run(TestContext* ctx);
void Test_Cubemap_Run(TestContext* ctx);
void Test_Capabilities_Run(TestContext* ctx);
void Test_NullBackend_Run(TestContext* ctx);
void Test_NativeHandle_Run(TestContext* ctx);
void Test_VulkanBackend_Run(TestContext* ctx);
void Test_OpenGLBackend_Run(TestContext* ctx);
void Test_BindGroup_Run(TestContext* ctx);
void Test_PipelineCacheFile_Run(TestContext* ctx);
void Test_Timestamps_Run(TestContext* ctx);
void Test_Validation_Run(TestContext* ctx);
#if defined(_WIN32)
// D3D12 doesn't exist outside Windows -- Test_D3D12Backend.c is only
// compiled into this executable on that platform (see CMakeLists.txt),
// so this declaration/call must stay behind the same guard.
void Test_D3D12Backend_Run(TestContext* ctx);
#endif

int main(void)
{
    TestContext ctx = { 0 };

    FLUXION_LOG_INFO("RHITests", "Running RHITests...");

    Test_BackendAvailability_Run(&ctx);
    Test_Handles_Run(&ctx);
    Test_Format_Run(&ctx);
    Test_Capabilities_Run(&ctx);
    Test_NullBackend_Run(&ctx);
    Test_NativeHandle_Run(&ctx);
    Test_VulkanBackend_Run(&ctx);
    Test_OpenGLBackend_Run(&ctx);
    Test_BindGroup_Run(&ctx);
    Test_PipelineCacheFile_Run(&ctx);
    Test_Timestamps_Run(&ctx);
    Test_Validation_Run(&ctx);
    Test_CompressedUpload_Run(&ctx);
    Test_Cubemap_Run(&ctx);
#if defined(_WIN32)
    Test_D3D12Backend_Run(&ctx);
#endif

    if (ctx.failures == 0)
    {
        FLUXION_LOG_INFO("RHITests", "All RHITests passed.");
        return 0;
    }

    FLUXION_LOG_ERROR("RHITests", "%d RHITests check(s) failed.", ctx.failures);
    return 1;
}
