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

#include <Fluxion/Foundation/Handle.h>
#include <Fluxion/Foundation/Handle.hpp>

#include <type_traits>

using Fluxion::Foundation::Handle;

namespace
{

struct TextureTag {};
struct BufferTag {};

} // namespace

FLUXION_DEFINE_HANDLE(TestRawHandle);

// main.c (plain C) calls this, so it needs unmangled C linkage.
extern "C" void Test_HandleCpp_Run(TestContext* ctx)
{
    Handle<TextureTag> invalid;
    TEST_CHECK(ctx, !invalid.IsValid());

    Handle<TextureTag> a{ 5, 1 };
    Handle<TextureTag> b{ 5, 1 };
    Handle<TextureTag> c{ 5, 2 };
    TEST_CHECK(ctx, a.IsValid());
    TEST_CHECK(ctx, a == b);
    TEST_CHECK(ctx, a != c);

    TestRawHandle raw = { 9, 3 };
    Handle<BufferTag> fromRaw = Handle<BufferTag>::FromRaw(raw);
    TEST_CHECK(ctx, fromRaw.index == 9 && fromRaw.generation == 3);

    TestRawHandle roundTrip = fromRaw.ToRaw<TestRawHandle>();
    TEST_CHECK(ctx, roundTrip.index == raw.index && roundTrip.generation == raw.generation);

    // Compile-time check: distinct tags are genuinely different types,
    // not just differently-named aliases of the same struct.
    static_assert(!std::is_same_v<Handle<TextureTag>, Handle<BufferTag>>);
}
