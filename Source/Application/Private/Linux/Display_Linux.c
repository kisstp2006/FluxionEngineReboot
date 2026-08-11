#include <Fluxion/Application/Window/Display.h>

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

static Display* Fluxion_DisplayConnection(void)
{
    static Display* s_display = NULL;
    if (!s_display)
    {
        s_display = XOpenDisplay(NULL);
    }
    return s_display;
}

u32 Fluxion_Display_GetCount(void)
{
    Display* display = Fluxion_DisplayConnection();
    if (!display)
    {
        return 0;
    }

    Window root = DefaultRootWindow(display);
    XRRScreenResources* resources = XRRGetScreenResourcesCurrent(display, root);
    if (!resources)
    {
        return 0;
    }

    u32 count = 0;
    for (int i = 0; i < resources->noutput; ++i)
    {
        XRROutputInfo* output = XRRGetOutputInfo(display, resources, resources->outputs[i]);
        if (output)
        {
            if (output->connection == RR_Connected && output->crtc)
            {
                count++;
            }
            XRRFreeOutputInfo(output);
        }
    }

    XRRFreeScreenResources(resources);
    return count;
}

bool Fluxion_Display_GetInfo(u32 index, FluxionDisplayInfo* outInfo)
{
    Display* display = Fluxion_DisplayConnection();
    if (!display)
    {
        return false;
    }

    Window root = DefaultRootWindow(display);
    XRRScreenResources* resources = XRRGetScreenResourcesCurrent(display, root);
    if (!resources)
    {
        return false;
    }

    RROutput primaryOutput = XRRGetOutputPrimary(display, root);

    bool found = false;
    u32 currentIndex = 0;

    for (int i = 0; i < resources->noutput && !found; ++i)
    {
        XRROutputInfo* output = XRRGetOutputInfo(display, resources, resources->outputs[i]);
        if (!output)
        {
            continue;
        }

        if (output->connection == RR_Connected && output->crtc)
        {
            if (currentIndex == index)
            {
                XRRCrtcInfo* crtc = XRRGetCrtcInfo(display, resources, output->crtc);
                if (crtc)
                {
                    outInfo->x = crtc->x;
                    outInfo->y = crtc->y;
                    outInfo->width = crtc->width;
                    outInfo->height = crtc->height;
                    // Xrandr doesn't expose a simple per-monitor DPI value
                    // the way Win32 does; refine later if a real need
                    // shows up (e.g. via the output's physical size).
                    outInfo->dpiScale = 1.0f;
                    outInfo->primary = (resources->outputs[i] == primaryOutput);
                    found = true;
                    XRRFreeCrtcInfo(crtc);
                }
            }
            currentIndex++;
        }

        XRRFreeOutputInfo(output);
    }

    XRRFreeScreenResources(resources);
    return found;
}
