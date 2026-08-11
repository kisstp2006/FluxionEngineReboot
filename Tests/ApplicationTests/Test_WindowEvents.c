#include "TestFramework.h"

#include <Fluxion/Application/Window/Window.h>
#include <Fluxion/Foundation/Defines.h>
#include <Fluxion/Foundation/Handle.h>

#include <string.h>

#if FLUXION_PLATFORM_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
#elif FLUXION_PLATFORM_LINUX
    #include <X11/Xlib.h>
    #include <stdint.h>
#endif

// Sends a real, native "please close" message to the OS window and checks
// that it comes back out the other end as a FluxionEvent — this proves
// the OS-message-to-FluxionEvent translation actually works, not just
// that the API compiles.
void Test_WindowEvents_Run(TestContext* ctx)
{
    FluxionEventQueue queue;
    Fluxion_EventQueue_Init(&queue, NULL, 64);
    Fluxion_WindowSystem_Init(NULL, &queue, 4);

    FluxionWindowDesc desc;
    desc.title = "Fluxion Window Events Test";
    desc.width = 320;
    desc.height = 240;
    desc.resizable = true;

    FluxionWindowHandle handle = Fluxion_Window_Create(&desc);
    TEST_CHECK(ctx, FLUXION_HANDLE_IS_VALID(handle));

    FluxionNativeWindowHandle native = Fluxion_Window_GetNativeHandle(handle);

#if FLUXION_PLATFORM_WINDOWS
    PostMessageW((HWND)native.value, WM_CLOSE, 0, 0);
#elif FLUXION_PLATFORM_LINUX
    Display* display = XOpenDisplay(NULL);
    if (display)
    {
        Atom wmDeleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", False);
        Atom wmProtocols = XInternAtom(display, "WM_PROTOCOLS", False);

        XEvent event;
        memset(&event, 0, sizeof(event));
        event.type = ClientMessage;
        event.xclient.window = (Window)(uintptr_t)native.value;
        event.xclient.message_type = wmProtocols;
        event.xclient.format = 32;
        event.xclient.data.l[0] = (long)wmDeleteWindow;
        event.xclient.data.l[1] = CurrentTime;

        XSendEvent(display, (Window)(uintptr_t)native.value, False, NoEventMask, &event);
        XFlush(display);
        XCloseDisplay(display);
    }
#endif

    Fluxion_WindowSystem_PollEvents();

    bool foundClose = false;
    FluxionEvent popped;
    while (Fluxion_EventQueue_Pop(&queue, &popped))
    {
        if (popped.type == FLUXION_EVENT_WINDOW_CLOSE_REQUESTED &&
            popped.window.index == handle.index && popped.window.generation == handle.generation)
        {
            foundClose = true;
        }
    }
    TEST_CHECK(ctx, foundClose);

    Fluxion_Window_Destroy(handle);
    Fluxion_WindowSystem_Shutdown();
    Fluxion_EventQueue_Destroy(&queue);
}
