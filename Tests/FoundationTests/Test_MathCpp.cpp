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

#include <Fluxion/Foundation/Math.hpp>

// main.c (plain C) calls this, so it needs unmangled C linkage.
extern "C" void Test_MathCpp_Run(TestContext* ctx)
{
    FluxionVec3 a{ 1.0f, 2.0f, 3.0f };
    FluxionVec3 b{ 4.0f, 5.0f, 6.0f };

    FluxionVec3 sum = a + b;
    TEST_CHECK(ctx, sum.x == 5.0f && sum.y == 7.0f && sum.z == 9.0f);

    FluxionVec3 diff = b - a;
    TEST_CHECK(ctx, diff.x == 3.0f && diff.y == 3.0f && diff.z == 3.0f);

    FluxionVec3 scaled = a * 2.0f;
    TEST_CHECK(ctx, scaled.x == 2.0f && scaled.y == 4.0f && scaled.z == 6.0f);

    FluxionVec3 scaledLeft = 2.0f * a;
    TEST_CHECK(ctx, scaledLeft.x == scaled.x && scaledLeft.y == scaled.y && scaledLeft.z == scaled.z);

    TEST_CHECK(ctx, Dot(a, b) == 32.0f); // 1*4 + 2*5 + 3*6

    FluxionVec3 crossResult = Cross(FluxionVec3{ 1.0f, 0.0f, 0.0f }, FluxionVec3{ 0.0f, 1.0f, 0.0f });
    TEST_CHECK(ctx, crossResult.x == 0.0f && crossResult.y == 0.0f && crossResult.z == 1.0f);

    FluxionVec3 unit{ 3.0f, 0.0f, 0.0f };
    TEST_CHECK(ctx, Length(unit) == 3.0f);
    FluxionVec3 normalized = Normalize(unit);
    TEST_CHECK(ctx, normalized.x == 1.0f && normalized.y == 0.0f && normalized.z == 0.0f);

    FluxionMat4 identity = Fluxion_Mat4_Identity();
    FluxionMat4 product = identity * identity;
    TEST_CHECK(ctx, product.m[0][0] == 1.0f && product.m[1][1] == 1.0f);

    FluxionQuat q = Fluxion_Quat_Identity();
    FluxionQuat qProduct = q * q;
    TEST_CHECK(ctx, qProduct.w == 1.0f);
}
