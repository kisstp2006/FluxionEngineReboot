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
