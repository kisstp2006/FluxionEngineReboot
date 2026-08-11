#include "TestFramework.h"

#include <Fluxion/Core/Startup/Subsystem.hpp>
#include <Fluxion/Core/Startup/SubsystemRegistry.h>

namespace
{

int g_startCount = 0;
int g_shutdownCount = 0;

class TestCppSubsystem final
{
public:
    static constexpr auto Name = "TestCppSubsystem";

    static FluxionResult Startup()
    {
        ++g_startCount;
        return Fluxion_ResultOk();
    }

    static void Shutdown()
    {
        ++g_shutdownCount;
    }
};

} // namespace

// main.c (plain C) calls this, so it needs unmangled C linkage.
extern "C" void Test_StartupCpp_Run(TestContext* ctx)
{
    g_startCount = 0;
    g_shutdownCount = 0;

    Fluxion_SubsystemRegistry_Init();
    {
        // Explicit call, not a namespace-scope static object's constructor
        // -- proves the C++ facade avoids static initialization order.
        bool registered = Fluxion::Core::RegisterSubsystem<TestCppSubsystem>(FLUXION_STARTUP_PHASE_CORE);
        TEST_CHECK(ctx, registered);

        FluxionResult startResult = Fluxion_SubsystemRegistry_StartupAll();
        TEST_CHECK(ctx, startResult.ok);
        TEST_CHECK(ctx, g_startCount == 1);

        const FluxionSubsystemId id = FLUXION_SUBSYSTEM_ID_OF(TestCppSubsystem);
        TEST_CHECK(ctx, Fluxion_SubsystemRegistry_IsRunning(id));

        Fluxion_SubsystemRegistry_ShutdownAll();
        TEST_CHECK(ctx, g_shutdownCount == 1);
        TEST_CHECK(ctx, !Fluxion_SubsystemRegistry_IsRunning(id));
    }
    Fluxion_SubsystemRegistry_Shutdown();
}
