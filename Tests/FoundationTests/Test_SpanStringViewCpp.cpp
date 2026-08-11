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
