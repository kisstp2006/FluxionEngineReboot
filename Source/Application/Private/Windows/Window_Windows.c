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

#include <Fluxion/Application/Window/Window.h>

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Defines.h>

#define WIN32_LEAN_AND_MEAN
// This file exclusively uses the *W (wide) Win32 API variants; UNICODE
// makes resource macros like IDC_ARROW resolve to their wide form too,
// matching LoadCursorW's LPCWSTR parameter.
#define UNICODE
#define _UNICODE
#include <Windows.h>

#include <string.h>

typedef struct FluxionWindowSlot
{
    HWND hwnd;
    u32 generation;
    bool inUse;
    bool fullscreen;
    bool cursorVisible;
    LONG savedStyle;
    RECT savedRect;

    // See Fluxion_Window_SetDrawWhileResizing. `drawing` guards against
    // the callback being entered from inside itself: what it draws can
    // take longer than the timer's period, and a second timer message
    // waiting behind the first would otherwise start a frame in the
    // middle of a frame.
    FluxionWindowDrawFrameFn drawWhileResizing;
    void* drawWhileResizingUserData;
    bool drawing;
} FluxionWindowSlot;

// The timer that gives the caller its turn while a drag is holding the
// loop. Its period is short enough to look continuous and long enough
// that a frame taking longer than one period is the ordinary case rather
// than a backlog.
#define FLUXION_WINDOW_RESIZE_TIMER_ID 1u
#define FLUXION_WINDOW_RESIZE_TIMER_MILLISECONDS 15u

static FluxionAllocator* s_allocator = NULL;
static FluxionEventQueue* s_eventQueue = NULL;
static FluxionWindowSlot* s_slots = NULL;
static usize s_slotCount = 0;
static ATOM s_windowClass = 0;
static const wchar_t* const FLUXION_WINDOW_CLASS_NAME = L"FluxionWindowClass";

static FluxionWindowHandle Fluxion_MakeHandle(usize index, u32 generation)
{
    FluxionWindowHandle handle;
    handle.index = (u32)index;
    handle.generation = generation;
    return handle;
}

static FluxionWindowSlot* Fluxion_ResolveSlot(FluxionWindowHandle handle)
{
    if (!FLUXION_HANDLE_IS_VALID(handle) || handle.index >= s_slotCount)
    {
        return NULL;
    }
    FluxionWindowSlot* slot = &s_slots[handle.index];
    if (!slot->inUse || slot->generation != handle.generation)
    {
        return NULL;
    }
    return slot;
}

static LRESULT CALLBACK Fluxion_WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        CREATESTRUCTW* create = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    usize index = (usize)(uintptr_t)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!s_eventQueue || index >= s_slotCount || !s_slots[index].inUse)
    {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    FluxionEvent event;
    memset(&event, 0, sizeof(event));
    event.window = Fluxion_MakeHandle(index, s_slots[index].generation);

    switch (message)
    {
        case WM_CLOSE:
            event.type = FLUXION_EVENT_WINDOW_CLOSE_REQUESTED;
            Fluxion_EventQueue_Push(s_eventQueue, &event);
            return 0;

        case WM_ENTERSIZEMOVE:
            // The drag begins here and the loop below this call does not
            // return until it ends -- so the turn has to come from a
            // timer, which is delivered inside that loop.
            if (s_slots[index].drawWhileResizing != NULL)
            {
                SetTimer(hwnd, FLUXION_WINDOW_RESIZE_TIMER_ID, FLUXION_WINDOW_RESIZE_TIMER_MILLISECONDS, NULL);
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);

        case WM_EXITSIZEMOVE:
            KillTimer(hwnd, FLUXION_WINDOW_RESIZE_TIMER_ID);
            return DefWindowProcW(hwnd, message, wParam, lParam);

        case WM_TIMER:
            if (wParam == FLUXION_WINDOW_RESIZE_TIMER_ID && s_slots[index].drawWhileResizing != NULL && !s_slots[index].drawing)
            {
                s_slots[index].drawing = true;
                s_slots[index].drawWhileResizing(event.window, s_slots[index].drawWhileResizingUserData);
                s_slots[index].drawing = false;
            }
            return 0;

        case WM_SIZE:
            event.type = FLUXION_EVENT_WINDOW_RESIZED;
            event.data.resized.width = LOWORD(lParam);
            event.data.resized.height = HIWORD(lParam);
            Fluxion_EventQueue_Push(s_eventQueue, &event);
            return 0;

        case WM_MOVE:
            event.type = FLUXION_EVENT_WINDOW_MOVED;
            event.data.moved.x = (i32)(short)LOWORD(lParam);
            event.data.moved.y = (i32)(short)HIWORD(lParam);
            Fluxion_EventQueue_Push(s_eventQueue, &event);
            return 0;

        case WM_SETFOCUS:
            event.type = FLUXION_EVENT_WINDOW_FOCUS_GAINED;
            Fluxion_EventQueue_Push(s_eventQueue, &event);
            return 0;

        case WM_KILLFOCUS:
            event.type = FLUXION_EVENT_WINDOW_FOCUS_LOST;
            Fluxion_EventQueue_Push(s_eventQueue, &event);
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            event.type = FLUXION_EVENT_KEY_DOWN;
            event.data.key.keyCode = (i32)wParam;
            event.data.key.repeat = (lParam & (1 << 30)) != 0;
            Fluxion_EventQueue_Push(s_eventQueue, &event);
            break;

        case WM_KEYUP:
        case WM_SYSKEYUP:
            event.type = FLUXION_EVENT_KEY_UP;
            event.data.key.keyCode = (i32)wParam;
            Fluxion_EventQueue_Push(s_eventQueue, &event);
            break;

        case WM_MOUSEMOVE:
            event.type = FLUXION_EVENT_MOUSE_MOVED;
            event.data.mouseMoved.x = (i32)(short)LOWORD(lParam);
            event.data.mouseMoved.y = (i32)(short)HIWORD(lParam);
            Fluxion_EventQueue_Push(s_eventQueue, &event);
            break;

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            event.type = FLUXION_EVENT_MOUSE_BUTTON_DOWN;
            event.data.mouseButton.button = (message == WM_LBUTTONDOWN) ? 0 : (message == WM_RBUTTONDOWN) ? 1 : 2;
            event.data.mouseButton.x = (i32)(short)LOWORD(lParam);
            event.data.mouseButton.y = (i32)(short)HIWORD(lParam);
            Fluxion_EventQueue_Push(s_eventQueue, &event);
            break;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            event.type = FLUXION_EVENT_MOUSE_BUTTON_UP;
            event.data.mouseButton.button = (message == WM_LBUTTONUP) ? 0 : (message == WM_RBUTTONUP) ? 1 : 2;
            event.data.mouseButton.x = (i32)(short)LOWORD(lParam);
            event.data.mouseButton.y = (i32)(short)HIWORD(lParam);
            Fluxion_EventQueue_Push(s_eventQueue, &event);
            break;

        case WM_MOUSEWHEEL:
            event.type = FLUXION_EVENT_MOUSE_SCROLLED;
            event.data.mouseScroll.deltaY = (f32)GET_WHEEL_DELTA_WPARAM(wParam) / (f32)WHEEL_DELTA;
            Fluxion_EventQueue_Push(s_eventQueue, &event);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void Fluxion_WindowSystem_Init(FluxionAllocator* allocator, FluxionEventQueue* eventQueue, usize maxWindows)
{
    FLUXION_ASSERT(eventQueue != NULL);
    FLUXION_ASSERT(maxWindows > 0);

    s_allocator = allocator ? allocator : Fluxion_DefaultAllocator();
    s_eventQueue = eventQueue;
    s_slotCount = maxWindows;
    s_slots = (FluxionWindowSlot*)Fluxion_Allocator_Alloc(s_allocator, maxWindows * sizeof(FluxionWindowSlot), FLUXION_DEFAULT_ALIGNMENT);
    memset(s_slots, 0, maxWindows * sizeof(FluxionWindowSlot));

    if (!s_windowClass)
    {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = Fluxion_WndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.lpszClassName = FLUXION_WINDOW_CLASS_NAME;
        s_windowClass = RegisterClassExW(&wc);
    }
}

void Fluxion_WindowSystem_Shutdown(void)
{
    for (usize i = 0; i < s_slotCount; ++i)
    {
        if (s_slots[i].inUse)
        {
            DestroyWindow(s_slots[i].hwnd);
        }
    }

    if (s_windowClass)
    {
        UnregisterClassW(FLUXION_WINDOW_CLASS_NAME, GetModuleHandleW(NULL));
        s_windowClass = 0;
    }

    Fluxion_Allocator_Free(s_allocator, s_slots, s_slotCount * sizeof(FluxionWindowSlot));
    s_slots = NULL;
    s_slotCount = 0;
    s_eventQueue = NULL;
}

FluxionWindowHandle Fluxion_Window_Create(const FluxionWindowDesc* desc)
{
    usize index = (usize)-1;
    for (usize i = 0; i < s_slotCount; ++i)
    {
        if (!s_slots[i].inUse)
        {
            index = i;
            break;
        }
    }

    FluxionWindowHandle invalid = { FLUXION_HANDLE_INVALID_INDEX, 0 };
    if (index == (usize)-1)
    {
        return invalid;
    }

    wchar_t wideTitle[256];
    MultiByteToWideChar(CP_UTF8, 0, desc->title ? desc->title : "", -1, wideTitle, FLUXION_ARRAY_COUNT(wideTitle));

    DWORD style = desc->resizable ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX));

    HWND hwnd = CreateWindowExW(
        0, FLUXION_WINDOW_CLASS_NAME, wideTitle, style,
        CW_USEDEFAULT, CW_USEDEFAULT, (int)desc->width, (int)desc->height,
        NULL, NULL, GetModuleHandleW(NULL), (LPVOID)(uintptr_t)index);

    if (!hwnd)
    {
        return invalid;
    }

    s_slots[index].hwnd = hwnd;
    s_slots[index].inUse = true;
    s_slots[index].fullscreen = false;
    s_slots[index].cursorVisible = true;
    s_slots[index].generation += 1;

    ShowWindow(hwnd, SW_SHOW);

    return Fluxion_MakeHandle(index, s_slots[index].generation);
}

void Fluxion_Window_Destroy(FluxionWindowHandle handle)
{
    FluxionWindowSlot* slot = Fluxion_ResolveSlot(handle);
    if (!slot)
    {
        return;
    }

    DestroyWindow(slot->hwnd);
    slot->hwnd = NULL;
    slot->inUse = false;
}

void Fluxion_WindowSystem_PollEvents(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void Fluxion_Window_SetTitle(FluxionWindowHandle handle, const char* title)
{
    FluxionWindowSlot* slot = Fluxion_ResolveSlot(handle);
    if (!slot)
    {
        return;
    }

    wchar_t wideTitle[256];
    MultiByteToWideChar(CP_UTF8, 0, title ? title : "", -1, wideTitle, FLUXION_ARRAY_COUNT(wideTitle));
    SetWindowTextW(slot->hwnd, wideTitle);
}

void Fluxion_Window_GetSize(FluxionWindowHandle handle, u32* outWidth, u32* outHeight)
{
    FluxionWindowSlot* slot = Fluxion_ResolveSlot(handle);
    if (!slot)
    {
        if (outWidth) *outWidth = 0;
        if (outHeight) *outHeight = 0;
        return;
    }

    RECT rect;
    GetClientRect(slot->hwnd, &rect);
    if (outWidth) *outWidth = (u32)(rect.right - rect.left);
    if (outHeight) *outHeight = (u32)(rect.bottom - rect.top);
}

void Fluxion_Window_SetFullscreen(FluxionWindowHandle handle, bool fullscreen)
{
    FluxionWindowSlot* slot = Fluxion_ResolveSlot(handle);
    if (!slot || slot->fullscreen == fullscreen)
    {
        return;
    }

    if (fullscreen)
    {
        slot->savedStyle = GetWindowLongW(slot->hwnd, GWL_STYLE);
        GetWindowRect(slot->hwnd, &slot->savedRect);

        HMONITOR monitor = MonitorFromWindow(slot->hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo;
        monitorInfo.cbSize = sizeof(monitorInfo);
        GetMonitorInfoW(monitor, &monitorInfo);

        SetWindowLongW(slot->hwnd, GWL_STYLE, (LONG)(slot->savedStyle & ~(WS_CAPTION | WS_THICKFRAME)));
        SetWindowPos(slot->hwnd, HWND_TOP,
            monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top,
            monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
            monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
            SWP_FRAMECHANGED);
    }
    else
    {
        SetWindowLongW(slot->hwnd, GWL_STYLE, slot->savedStyle);
        SetWindowPos(slot->hwnd, NULL,
            slot->savedRect.left, slot->savedRect.top,
            slot->savedRect.right - slot->savedRect.left,
            slot->savedRect.bottom - slot->savedRect.top,
            SWP_FRAMECHANGED | SWP_NOZORDER);
    }

    slot->fullscreen = fullscreen;
}

bool Fluxion_Window_IsFullscreen(FluxionWindowHandle handle)
{
    FluxionWindowSlot* slot = Fluxion_ResolveSlot(handle);
    return slot ? slot->fullscreen : false;
}

void Fluxion_Window_SetCursorVisible(FluxionWindowHandle handle, bool visible)
{
    FluxionWindowSlot* slot = Fluxion_ResolveSlot(handle);
    if (!slot || slot->cursorVisible == visible)
    {
        return;
    }

    ShowCursor(visible ? TRUE : FALSE);
    slot->cursorVisible = visible;
}

FluxionNativeWindowHandle Fluxion_Window_GetNativeHandle(FluxionWindowHandle handle)
{
    FluxionWindowSlot* slot = Fluxion_ResolveSlot(handle);
    FluxionNativeWindowHandle native;
    native.value = slot ? (void*)slot->hwnd : NULL;
    return native;
}

void Fluxion_Window_SetDrawWhileResizing(FluxionWindowHandle handle, FluxionWindowDrawFrameFn callback, void* userData)
{
    FluxionWindowSlot* slot = Fluxion_ResolveSlot(handle);
    if (!slot) return;
    slot->drawWhileResizing = callback;
    slot->drawWhileResizingUserData = userData;
}

void* Fluxion_WindowSystem_GetNativeDisplayHandle(void)
{
    // HWNDs are self-sufficient on Windows -- a Vulkan backend gets the
    // HINSTANCE it needs from GetModuleHandle(NULL) instead of here.
    return NULL;
}
