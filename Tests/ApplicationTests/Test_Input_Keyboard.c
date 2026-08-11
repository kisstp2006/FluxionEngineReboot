#include "TestFramework.h"

#include <Fluxion/Application/Input/Input.h>
#include <Fluxion/Application/Window/Window.h>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Handle.h>

#include <string.h>

#if FLUXION_PLATFORM_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
#elif FLUXION_PLATFORM_LINUX
    #include <X11/Xlib.h>
    #include <X11/keysym.h>
    #include <stdint.h>
#endif

static void Fluxion_SendKeyEvent(FluxionNativeWindowHandle native, bool down)
{
#if FLUXION_PLATFORM_WINDOWS
    PostMessageW((HWND)native.value, down ? WM_KEYDOWN : WM_KEYUP, (WPARAM)'W', 0);
#elif FLUXION_PLATFORM_LINUX
    Display* display = XOpenDisplay(NULL);
    if (!display)
    {
        return;
    }

    KeyCode keycode = XKeysymToKeycode(display, XK_W);

    XEvent event;
    memset(&event, 0, sizeof(event));
    event.type = down ? KeyPress : KeyRelease;
    event.xkey.window = (Window)(uintptr_t)native.value;
    event.xkey.root = DefaultRootWindow(display);
    event.xkey.keycode = keycode;
    event.xkey.time = CurrentTime;

    XSendEvent(display, (Window)(uintptr_t)native.value, False, down ? KeyPressMask : KeyReleaseMask, &event);
    XFlush(display);
    XCloseDisplay(display);
#else
    FLUXION_UNUSED(native);
    FLUXION_UNUSED(down);
#endif
}

// Sends a real, native "W key" press/release to the OS window and checks
// it comes back out as Input state — same OS-message-to-engine proof as
// Test_WindowEvents.c, extended through Fluxion_Input_ProcessEvent.
void Test_Input_Keyboard_Run(TestContext* ctx)
{
    FluxionEventQueue queue;
    Fluxion_EventQueue_Init(&queue, NULL, 64);
    Fluxion_WindowSystem_Init(NULL, &queue, 4);
    Fluxion_Input_Init();

    FluxionWindowDesc desc;
    desc.title = "Fluxion Input Keyboard Test";
    desc.width = 320;
    desc.height = 240;
    desc.resizable = true;

    FluxionWindowHandle handle = Fluxion_Window_Create(&desc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(handle));
    FluxionNativeWindowHandle native = Fluxion_Window_GetNativeHandle(handle);

    Fluxion_Input_BeginFrame();
    TEST_CHECK(ctx, !Fluxion_Input_IsKeyDown(FLUXION_KEY_W));

    Fluxion_SendKeyEvent(native, true);
    Fluxion_WindowSystem_PollEvents();
    {
        FluxionEvent popped;
        while (Fluxion_EventQueue_Pop(&queue, &popped))
        {
            Fluxion_Input_ProcessEvent(&popped);
        }
    }

    TEST_CHECK(ctx, Fluxion_Input_IsKeyDown(FLUXION_KEY_W));
    TEST_CHECK(ctx, Fluxion_Input_WasKeyPressed(FLUXION_KEY_W));

    // Next frame: still held, but no longer "just pressed".
    Fluxion_Input_BeginFrame();
    TEST_CHECK(ctx, Fluxion_Input_IsKeyDown(FLUXION_KEY_W));
    TEST_CHECK(ctx, !Fluxion_Input_WasKeyPressed(FLUXION_KEY_W));

    Fluxion_SendKeyEvent(native, false);
    Fluxion_WindowSystem_PollEvents();
    {
        FluxionEvent popped;
        while (Fluxion_EventQueue_Pop(&queue, &popped))
        {
            Fluxion_Input_ProcessEvent(&popped);
        }
    }

    TEST_CHECK(ctx, !Fluxion_Input_IsKeyDown(FLUXION_KEY_W));
    TEST_CHECK(ctx, Fluxion_Input_WasKeyReleased(FLUXION_KEY_W));

    Fluxion_Window_Destroy(handle);
    Fluxion_Input_Shutdown();
    Fluxion_WindowSystem_Shutdown();
    Fluxion_EventQueue_Destroy(&queue);
}
