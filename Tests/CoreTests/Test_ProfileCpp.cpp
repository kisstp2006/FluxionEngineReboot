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

#include <cstring>

#include <Fluxion/Core/Diagnostics/ProfileScope.hpp>

namespace
{

char s_callLog[128];
usize s_callLogLength = 0;

void AppendToLog(char c)
{
    if (s_callLogLength + 1 >= sizeof(s_callLog)) return;
    s_callLog[s_callLogLength++] = c;
    s_callLog[s_callLogLength] = '\0';
}

void TestZoneBegin(const FluxionSourceLocation*, const char*, void*)
{
    AppendToLog('B');
}

void TestZoneEnd(void*)
{
    AppendToLog('E');
}

void RunNestedScopes()
{
    FLUXION_PROFILE_FUNCTION();
    {
        FLUXION_PROFILE_SCOPE("Inner");
    }
}

} // namespace

// main.c (plain C) calls this, so it needs unmangled C linkage.
extern "C" void Test_ProfileCpp_Run(TestContext* ctx)
{
    s_callLogLength = 0;
    s_callLog[0] = '\0';

    FluxionProfileBackend backend = {};
    backend.zoneBegin = TestZoneBegin;
    backend.zoneEnd = TestZoneEnd;
    Fluxion_Profiler_SetBackend(&backend);

    RunNestedScopes();

    // RunNestedScopes' own FLUXION_PROFILE_FUNCTION() begins first and
    // ends last; the nested FLUXION_PROFILE_SCOPE("Inner") begins and
    // ends fully inside it -- proving the nesting order comes from C++
    // scope/destructor order, not any bookkeeping in the C API itself.
    TEST_CHECK(ctx, std::strcmp(s_callLog, "BBEE") == 0);

    Fluxion_Profiler_ClearBackend();
}
