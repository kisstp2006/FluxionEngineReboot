#include "TestFramework.h"

#include <Fluxion/Core/Service/Service.hpp>
#include <Fluxion/Core/Service/ServiceRegistry.h>

namespace
{

struct TestCppService
{
    FluxionServiceHeader header;
    static constexpr auto Name = "TestCppService";
    static constexpr u32 Version = 1;

    i32 value = 0;
};

} // namespace

// main.c (plain C) calls this, so it needs unmangled C linkage.
extern "C" void Test_ServiceCpp_Run(TestContext* ctx)
{
    Fluxion_ServiceRegistry_Init();
    {
        TestCppService service;
        service.value = 7;

        TEST_CHECK(ctx, Fluxion::Core::RegisterService(service));

        const TestCppService* found = Fluxion::Core::GetService<TestCppService>();
        TEST_CHECK(ctx, found == &service);
        TEST_CHECK(ctx, found != nullptr && found->value == 7);

        Fluxion::Core::UnregisterService<TestCppService>();
        TEST_CHECK(ctx, Fluxion::Core::GetService<TestCppService>() == nullptr);
    }
    Fluxion_ServiceRegistry_Shutdown();
}
