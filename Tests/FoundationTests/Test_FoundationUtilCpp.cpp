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

#include <Fluxion/Foundation/BitFlags.hpp>
#include <Fluxion/Foundation/FunctionRef.hpp>
#include <Fluxion/Foundation/Memory/AllocatorRef.hpp>
#include <Fluxion/Foundation/ScopeExit.hpp>
#include <Fluxion/Foundation/Types.h>

using namespace Fluxion::Foundation;

namespace
{

enum class TestFlag : u32
{
    None = 0,
    A = 1u << 0,
    B = 1u << 1,
    C = 1u << 2,
};

i32 CallFunctionRef(FunctionRef<i32(i32)> fn, i32 value)
{
    return fn(value);
}

} // namespace

// main.c (plain C) calls this, so it needs unmangled C linkage.
extern "C" void Test_FoundationUtilCpp_Run(TestContext* ctx)
{
    // AllocatorRef
    {
        AllocatorRef allocator;
        void* block = allocator.Alloc(64, 16);
        TEST_CHECK(ctx, block != nullptr);
        allocator.Free(block, 64);
    }

    // ScopeExit
    {
        bool ran = false;
        {
            FLUXION_SCOPE_EXIT(ran = true);
        }
        TEST_CHECK(ctx, ran);

        bool dismissedRan = false;
        {
            auto guard = MakeScopeExit([&]() { dismissedRan = true; });
            guard.Dismiss();
        }
        TEST_CHECK(ctx, !dismissedRan);
    }

    // FunctionRef
    {
        i32 captured = 10;
        auto lambda = [captured](i32 x) { return x + captured; };
        TEST_CHECK(ctx, CallFunctionRef(lambda, 5) == 15);
    }

    // BitFlags
    {
        BitFlags<TestFlag> flags;
        TEST_CHECK(ctx, !flags.Any());

        flags.Set(TestFlag::A).Set(TestFlag::C);
        TEST_CHECK(ctx, flags.Has(TestFlag::A));
        TEST_CHECK(ctx, !flags.Has(TestFlag::B));
        TEST_CHECK(ctx, flags.Has(TestFlag::C));

        flags.Clear(TestFlag::A);
        TEST_CHECK(ctx, !flags.Has(TestFlag::A));

        BitFlags<TestFlag> combined = BitFlags<TestFlag>(TestFlag::A) | BitFlags<TestFlag>(TestFlag::B);
        TEST_CHECK(ctx, combined.Has(TestFlag::A) && combined.Has(TestFlag::B));
    }
}
