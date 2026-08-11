#include <Fluxion/Application/Window/Clipboard.h>

#include <X11/Xlib.h>

#include <string.h>

// v1 uses the legacy XA_CUT_BUFFER0 mechanism (XStoreBytes/XFetchBytes):
// simple and synchronous, works for round-tripping within our own
// process/session. The full CLIPBOARD selection-owner protocol (async,
// answering SelectionRequest events from other clients) is more
// interoperable with modern clipboard managers but a lot more machinery;
// swapping it in later wouldn't need to change this header's API.
static Display* Fluxion_ClipboardDisplay(void)
{
    static Display* s_display = NULL;
    if (!s_display)
    {
        s_display = XOpenDisplay(NULL);
    }
    return s_display;
}

bool Fluxion_Clipboard_SetText(const char* text)
{
    Display* display = Fluxion_ClipboardDisplay();
    if (!display || !text)
    {
        return false;
    }

    XStoreBytes(display, text, (int)strlen(text));
    XFlush(display);
    return true;
}

bool Fluxion_Clipboard_GetText(char* outBuffer, usize bufferSize)
{
    Display* display = Fluxion_ClipboardDisplay();
    if (!display || bufferSize == 0)
    {
        return false;
    }

    int length = 0;
    char* bytes = XFetchBytes(display, &length);
    if (!bytes)
    {
        return false;
    }

    bool ok = (usize)length < bufferSize;
    if (ok)
    {
        memcpy(outBuffer, bytes, (usize)length);
        outBuffer[length] = '\0';
    }

    XFree(bytes);
    return ok;
}
