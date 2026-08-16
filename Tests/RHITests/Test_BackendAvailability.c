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

#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/RHI/RHI.h>

// Whether a build contains a backend is decided in the build files and
// read back through one function. The two can disagree -- a source
// directory dropped from the compile without the flag following it, or
// the other way round -- and nothing would say so, because a caller
// asking for a missing backend just gets an invalid handle, which is also
// what a missing driver gives.
//
// So the answer is checked against something that cannot be edited
// independently of it: which operating system this is.

void Test_BackendAvailability_Run(TestContext* ctx)
{
    // The one that talks to nothing is on every target. Without this the
    // engine would have no way to run a test or a tool on a machine with
    // no GPU at all.
    TEST_CHECK(ctx, Fluxion_RHI_IsBackendAvailable(FLUXION_RHI_BACKEND_NULL));

    // Vulkan is the backend the portable targets are reached through, so
    // it is built everywhere. Whether a driver answers is a separate
    // question, asked at instance creation, not here.
    TEST_CHECK(ctx, Fluxion_RHI_IsBackendAvailable(FLUXION_RHI_BACKEND_VULKAN));

    // D3D12 is a Windows API and exists nowhere else. Both directions are
    // checked: present on Windows, absent everywhere else -- one without
    // the other would pass for a build that always said yes.
#if FLUXION_PLATFORM_WINDOWS
    TEST_CHECK(ctx, Fluxion_RHI_IsBackendAvailable(FLUXION_RHI_BACKEND_D3D12));
#else
    TEST_CHECK(ctx, !Fluxion_RHI_IsBackendAvailable(FLUXION_RHI_BACKEND_D3D12));
#endif

    // The OpenGL backend is desktop GL through WGL or GLX, so it goes
    // with having a desktop window system rather than with any particular
    // OS.
#if FLUXION_PLATFORM_WINDOWS || FLUXION_PLATFORM_LINUX
    TEST_CHECK(ctx, Fluxion_RHI_IsBackendAvailable(FLUXION_RHI_BACKEND_OPENGL));
#else
    TEST_CHECK(ctx, !Fluxion_RHI_IsBackendAvailable(FLUXION_RHI_BACKEND_OPENGL));
#endif

    // A number that names no backend is not available, rather than
    // reading off the end of whatever table answers the question.
    TEST_CHECK(ctx, !Fluxion_RHI_IsBackendAvailable((FluxionRHIBackendType)0x7FFFFFFF));

    // And the answer has to match what actually happens. Every backend
    // this build says it has can at least be asked; the null one is used
    // for the reverse, since it is the only backend whose creation cannot
    // fail for reasons outside the engine.
    {
        FluxionRHIInstanceDesc desc;
        desc.applicationName = "RHITests.BackendAvailability";
        desc.enableValidation = false;

        FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &desc);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(instance));
        Fluxion_RHI_DestroyInstance(instance);
    }
}
