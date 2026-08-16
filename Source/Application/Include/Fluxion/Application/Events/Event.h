// The contents of this file are subject to the Common Public Attribution
// License Version 1.0 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// https://opensource.org/license/cpal-1-0. The License is based on the
// Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
// to cover use of software over a computer network and provide for limited
// attribution for the Original Developer. In addition, Exhibit A has been
// modified to be consistent with Exhibit B.
//
// Software distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
// for the specific language governing rights and limitations under the
// License.
//
// The Original Code is Fluxion Engine.
//
// The Original Developer is not the Initial Developer and is __________. If
// left blank, the Original Developer is the Initial Developer.
//
// The Initial Developer of the Original Code is Kiss Tibor Péter. All
// portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
// All Rights Reserved.
//
// Contributor ______________________.
//
// Alternatively, the contents of this file may be used under the terms of
// the Fluxion Engine Commercial License Agreement Version 1.0, separately
// obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
// License"), in which case the provisions of the Commercial License are
// applicable instead of those above.
//
// If you wish to allow use of your version of this file only under the terms
// of the Commercial License and not to allow others to use your version of
// this file under the CPAL, indicate your decision by deleting the
// provisions above and replace them with the notice and other provisions
// required by the Commercial License. If you do not delete the provisions
// above, a recipient may use your version of this file under either the CPAL
// or the Commercial License.
//
// SPDX-License-Identifier: CPAL-1.0

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
