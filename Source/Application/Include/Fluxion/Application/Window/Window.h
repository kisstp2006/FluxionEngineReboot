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

#include <Fluxion/Application/Events/EventQueue.h>
#include <Fluxion/Application/Window/WindowHandle.h>
#include <Fluxion/Foundation/Memory/Allocator.h>
#include <Fluxion/Foundation/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FluxionWindowDesc
{
    const char* title;
    u32 width;
    u32 height;
    bool resizable;
} FluxionWindowDesc;

// Escape hatch for code that genuinely needs the native handle (HWND on
// Windows, an X11 Window on Linux, stored as the integer value cast
// through a pointer-sized type) — e.g. to hand a swap chain surface to a
// future RHI. Everything else should go through the handle-based API.
typedef struct FluxionNativeWindowHandle
{
    void* value;
} FluxionNativeWindowHandle;

// `eventQueue` receives every event from every window created after this
// call, until Shutdown. `maxWindows` sizes a fixed-capacity window table
// allocated once here (one allocation, freed at Shutdown) — PollEvents
// runs every frame, so the table stays a flat, non-reallocating array
// rather than something that can move under it.
void Fluxion_WindowSystem_Init(FluxionAllocator* allocator, FluxionEventQueue* eventQueue, usize maxWindows);
void Fluxion_WindowSystem_Shutdown(void);

FluxionWindowHandle Fluxion_Window_Create(const FluxionWindowDesc* desc);
void Fluxion_Window_Destroy(FluxionWindowHandle handle);

// Pumps pending OS messages for every live window, pushing FluxionEvents
// into the queue passed to Init. Non-blocking — returns immediately once
// the OS has no more pending messages.
void Fluxion_WindowSystem_PollEvents(void);

void Fluxion_Window_SetTitle(FluxionWindowHandle handle, const char* title);
void Fluxion_Window_GetSize(FluxionWindowHandle handle, u32* outWidth, u32* outHeight);

// Borderless-style fullscreen (resize to cover the monitor), not an
// exclusive display-mode switch — faster to toggle and behaves better
// alongside other windows/compositors.
void Fluxion_Window_SetFullscreen(FluxionWindowHandle handle, bool fullscreen);
bool Fluxion_Window_IsFullscreen(FluxionWindowHandle handle);

void Fluxion_Window_SetCursorVisible(FluxionWindowHandle handle, bool visible);

FluxionNativeWindowHandle Fluxion_Window_GetNativeHandle(FluxionWindowHandle handle);

// DRAWS ONE FRAME, called from inside the window itself.
typedef void (*FluxionWindowDrawFrameFn)(FluxionWindowHandle window, void* userData);

// A frame drawn while the user is dragging the window's edge.
//
// Dragging an edge starts a loop belonging to the windowing system, and
// it does not end until the mouse is let go. A caller's own loop -- the
// one that polls events and draws -- gets no turn in all that time, so
// the picture stops dead and the window shows whatever it last had,
// stretched. This is the turn it does not otherwise get: the window
// calls back while the drag is happening, and what the callback draws is
// what the user sees follow the mouse.
//
// Not every platform needs it. Where dragging does not take the loop
// away, nothing calls this and the caller's own loop keeps drawing --
// which is the same behaviour by a shorter road.
//
// The callback draws; it must NOT poll events, because the events are
// what it is being called from the middle of.
void Fluxion_Window_SetDrawWhileResizing(FluxionWindowHandle handle, FluxionWindowDrawFrameFn callback, void* userData);

// A second native escape hatch, at the window-*system* level rather than
// per-window: some native surface APIs (e.g. Vulkan's
// vkCreateXlibSurfaceKHR) need the connection/display object a window
// belongs to, not just the window itself. Returns NULL on Windows (HWNDs
// are self-sufficient there -- a Vulkan backend gets HINSTANCE from
// GetModuleHandle(NULL) instead) and the X11 Display* on Linux.
void* Fluxion_WindowSystem_GetNativeDisplayHandle(void);

#ifdef __cplusplus
}
#endif
