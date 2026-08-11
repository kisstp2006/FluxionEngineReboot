#include "TestFramework.h"

#include <Fluxion/Application/Window/Window.h>
#include <Fluxion/Foundation/Handle.h>

void Test_Window_Run(TestContext* ctx)
{
    FluxionEventQueue queue;
    Fluxion_EventQueue_Init(&queue, NULL, 64);
    Fluxion_WindowSystem_Init(NULL, &queue, 4);

    FluxionWindowDesc desc;
    desc.title = "Fluxion Window Test";
    desc.width = 320;
    desc.height = 240;
    desc.resizable = true;

    FluxionWindowHandle handle = Fluxion_Window_Create(&desc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(handle));

    u32 width = 0, height = 0;
    Fluxion_Window_GetSize(handle, &width, &height);
    TEST_CHECK(ctx, width > 0 && height > 0);

    Fluxion_Window_SetTitle(handle, "Renamed");

    FluxionNativeWindowHandle native = Fluxion_Window_GetNativeHandle(handle);
    TEST_CHECK(ctx, native.value != NULL);

    TEST_CHECK(ctx, Fluxion_Window_IsFullscreen(handle) == false);

    Fluxion_WindowSystem_PollEvents(); // must not crash with a live window

    Fluxion_Window_Destroy(handle);

    // A destroyed handle must no longer resolve.
    FluxionNativeWindowHandle destroyedNative = Fluxion_Window_GetNativeHandle(handle);
    TEST_CHECK(ctx, destroyedNative.value == NULL);

    Fluxion_WindowSystem_PollEvents(); // must also be safe with zero live windows

    Fluxion_WindowSystem_Shutdown();
    Fluxion_EventQueue_Destroy(&queue);
}
