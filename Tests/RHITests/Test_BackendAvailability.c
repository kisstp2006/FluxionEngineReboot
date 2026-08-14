#include "TestFramework.h"

#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/RHI/RHI.h>

// Whether a build contains a backend is decided in the build files and
// read back through one function. The two can disagree -- a source
// directory dropped from the compile without the flag following it, or
// the other way round -- and nothing would say so, because a caller
// asking for a missing backend just gets an invalid handle, which is also
// what a missing driver gives.
//
// So the answer is checked against something that cannot be edited
// independently of it: which operating system this is.

void Test_BackendAvailability_Run(TestContext* ctx)
{
    // The one that talks to nothing is on every target. Without this the
    // engine would have no way to run a test or a tool on a machine with
    // no GPU at all.
    TEST_CHECK(ctx, Fluxion_RHI_IsBackendAvailable(FLUXION_RHI_BACKEND_NULL));

    // Vulkan is the backend the portable targets are reached through, so
    // it is built everywhere. Whether a driver answers is a separate
    // question, asked at instance creation, not here.
    TEST_CHECK(ctx, Fluxion_RHI_IsBackendAvailable(FLUXION_RHI_BACKEND_VULKAN));

    // D3D12 is a Windows API and exists nowhere else. Both directions are
    // checked: present on Windows, absent everywhere else -- one without
    // the other would pass for a build that always said yes.
#if FLUXION_PLATFORM_WINDOWS
    TEST_CHECK(ctx, Fluxion_RHI_IsBackendAvailable(FLUXION_RHI_BACKEND_D3D12));
#else
    TEST_CHECK(ctx, !Fluxion_RHI_IsBackendAvailable(FLUXION_RHI_BACKEND_D3D12));
#endif

    // The OpenGL backend is desktop GL through WGL or GLX, so it goes
    // with having a desktop window system rather than with any particular
    // OS.
#if FLUXION_PLATFORM_WINDOWS || FLUXION_PLATFORM_LINUX
    TEST_CHECK(ctx, Fluxion_RHI_IsBackendAvailable(FLUXION_RHI_BACKEND_OPENGL));
#else
    TEST_CHECK(ctx, !Fluxion_RHI_IsBackendAvailable(FLUXION_RHI_BACKEND_OPENGL));
#endif

    // A number that names no backend is not available, rather than
    // reading off the end of whatever table answers the question.
    TEST_CHECK(ctx, !Fluxion_RHI_IsBackendAvailable((FluxionRHIBackendType)0x7FFFFFFF));

    // And the answer has to match what actually happens. Every backend
    // this build says it has can at least be asked; the null one is used
    // for the reverse, since it is the only backend whose creation cannot
    // fail for reasons outside the engine.
    {
        FluxionRHIInstanceDesc desc;
        desc.applicationName = "RHITests.BackendAvailability";
        desc.enableValidation = false;

        FluxionRHIInstanceHandle instance = Fluxion_RHI_CreateInstance(FLUXION_RHI_BACKEND_NULL, &desc);
        TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(instance));
        Fluxion_RHI_DestroyInstance(instance);
    }
}
