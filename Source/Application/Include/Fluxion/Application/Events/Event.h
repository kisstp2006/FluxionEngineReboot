#pragma once

#include <Fluxion/Application/Window/WindowHandle.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FluxionEventType
{
    FLUXION_EVENT_WINDOW_CLOSE_REQUESTED = 0,
    FLUXION_EVENT_WINDOW_RESIZED,
    FLUXION_EVENT_WINDOW_MOVED,
    FLUXION_EVENT_WINDOW_FOCUS_GAINED,
    FLUXION_EVENT_WINDOW_FOCUS_LOST,
    FLUXION_EVENT_KEY_DOWN,   // raw platform key code, no mapping — that's Input (later)
    FLUXION_EVENT_KEY_UP,
    FLUXION_EVENT_MOUSE_MOVED,
    FLUXION_EVENT_MOUSE_BUTTON_DOWN,
    FLUXION_EVENT_MOUSE_BUTTON_UP,
    FLUXION_EVENT_MOUSE_SCROLLED,
} FluxionEventType;

// POD, fixed size, no owned memory — cheap to copy into/out of the ring
// buffer in FluxionEventQueue.
typedef struct FluxionEvent
{
    FluxionEventType type;
    FluxionWindowHandle window;
    union
    {
        struct { u32 width; u32 height; } resized;
        struct { i32 x; i32 y; } moved;
        struct { i32 keyCode; bool repeat; } key;
        struct { i32 x; i32 y; } mouseMoved;
        struct { i32 button; i32 x; i32 y; } mouseButton;
        struct { f32 deltaX; f32 deltaY; } mouseScroll;
    } data;
} FluxionEvent;

#ifdef __cplusplus
}
#endif
