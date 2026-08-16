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

#include <Fluxion/Foundation/UUID.h>

#include <string.h>

void Test_UUID_Run(TestContext* ctx)
{
    FluxionUUID a = Fluxion_UUID_Generate();
    FluxionUUID b = Fluxion_UUID_Generate();

    TEST_CHECK(ctx, !Fluxion_UUID_IsNil(a));
    TEST_CHECK(ctx, Fluxion_UUID_Equals(a, a));
    TEST_CHECK(ctx, !Fluxion_UUID_Equals(a, b));

    // RFC 4122 version 4 / variant bits.
    TEST_CHECK(ctx, (a.bytes[6] & 0xF0u) == 0x40u);
    TEST_CHECK(ctx, (a.bytes[8] & 0xC0u) == 0x80u);

    char buffer[37];
    Fluxion_UUID_ToString(a, buffer);
    TEST_CHECK(ctx, strlen(buffer) == 36);
    TEST_CHECK(ctx, buffer[8] == '-' && buffer[13] == '-' && buffer[18] == '-' && buffer[23] == '-');

    FluxionUUID nilId;
    memset(nilId.bytes, 0, sizeof(nilId.bytes));
    TEST_CHECK(ctx, Fluxion_UUID_IsNil(nilId));
}
