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
