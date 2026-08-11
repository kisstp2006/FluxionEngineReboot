#include <Fluxion/Application/Window/Display.h>

#include <Fluxion/Foundation/Defines.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <ShellScalingApi.h>

#include <string.h>

#define FLUXION_MAX_DISPLAYS 16

typedef struct FluxionEnumContext
{
    FluxionDisplayInfo displays[FLUXION_MAX_DISPLAYS];
    u32 count;
} FluxionEnumContext;

static BOOL CALLBACK Fluxion_MonitorEnumProc(HMONITOR monitor, HDC hdc, LPRECT rect, LPARAM lParam)
{
    FLUXION_UNUSED(hdc);
    FLUXION_UNUSED(rect);

    FluxionEnumContext* context = (FluxionEnumContext*)lParam;
    if (context->count >= FLUXION_MAX_DISPLAYS)
    {
        return FALSE;
    }

    MONITORINFOEXW info;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, (MONITORINFO*)&info))
    {
        return TRUE;
    }

    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    FLUXION_UNUSED(dpiY);

    FluxionDisplayInfo* display = &context->displays[context->count++];
    display->x = info.rcMonitor.left;
    display->y = info.rcMonitor.top;
    display->width = (u32)(info.rcMonitor.right - info.rcMonitor.left);
    display->height = (u32)(info.rcMonitor.bottom - info.rcMonitor.top);
    display->dpiScale = (f32)dpiX / 96.0f;
    display->primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;

    return TRUE;
}

static u32 Fluxion_EnumerateDisplays(FluxionEnumContext* context)
{
    memset(context, 0, sizeof(*context));
    EnumDisplayMonitors(NULL, NULL, Fluxion_MonitorEnumProc, (LPARAM)context);
    return context->count;
}

u32 Fluxion_Display_GetCount(void)
{
    FluxionEnumContext context;
    return Fluxion_EnumerateDisplays(&context);
}

bool Fluxion_Display_GetInfo(u32 index, FluxionDisplayInfo* outInfo)
{
    FluxionEnumContext context;
    u32 count = Fluxion_EnumerateDisplays(&context);
    if (index >= count)
    {
        return false;
    }
    *outInfo = context.displays[index];
    return true;
}
