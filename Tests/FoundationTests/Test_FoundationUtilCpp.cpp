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
