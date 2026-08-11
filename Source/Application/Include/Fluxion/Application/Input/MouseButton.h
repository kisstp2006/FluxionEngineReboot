#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Matches the `button` field already produced by both Window backends
// (Window_Windows.c, Window_Linux.c) in FluxionEvent.data.mouseButton.
typedef enum FluxionMouseButton
{
    FLUXION_MOUSE_BUTTON_LEFT = 0,
    FLUXION_MOUSE_BUTTON_RIGHT = 1,
    FLUXION_MOUSE_BUTTON_MIDDLE = 2,
    FLUXION_MOUSE_BUTTON_X1 = 3,
    FLUXION_MOUSE_BUTTON_X2 = 4,
    FLUXION_MOUSE_BUTTON_COUNT,
} FluxionMouseButton;

#ifdef __cplusplus
}
#endif
