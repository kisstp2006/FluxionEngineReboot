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

#include <Fluxion/Foundation/Containers/Span.hpp>
#include <Fluxion/Foundation/Containers/StringView.hpp>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Types.h>

#include <string_view>

using namespace Fluxion::Foundation;

// main.c (plain C) calls this, so it needs unmangled C linkage.
extern "C" void Test_SpanStringViewCpp_Run(TestContext* ctx)
{
    i32 values[] = { 1, 2, 3, 4 };
    FluxionSpan span = Fluxion_Span_Make(values, 4, sizeof(i32));

    std::span<i32> stdSpan = ToStdSpan<i32>(span);
    TEST_CHECK(ctx, stdSpan.size() == 4);
    i32 sum = 0;
    for (i32 v : stdSpan) { sum += v; }
    TEST_CHECK(ctx, sum == 10);

    FluxionSpan roundTrip = FromStdSpan<i32>(stdSpan);
    TEST_CHECK(ctx, roundTrip.data == span.data && roundTrip.count == span.count);

    FluxionStringView view = Fluxion_StringView_FromCStr("hello");
    std::string_view stdView = ToStdStringView(view);
    TEST_CHECK(ctx, stdView == "hello");

    FluxionStringView roundTripView = FromStdStringView(stdView);
    TEST_CHECK(ctx, Fluxion_StringView_Equals(roundTripView, view));

    // Range-for directly over FluxionStringView via the ADL begin()/end().
    usize charCount = 0;
    for (char c : view) { FLUXION_UNUSED(c); ++charCount; }
    TEST_CHECK(ctx, charCount == 5);
}
