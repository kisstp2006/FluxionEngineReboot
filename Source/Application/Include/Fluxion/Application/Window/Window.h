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

#ifdef __cplusplus
}
#endif
