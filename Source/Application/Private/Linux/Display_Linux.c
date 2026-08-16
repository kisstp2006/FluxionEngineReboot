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
