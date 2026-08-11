#include <Fluxion/Application/Window/Window.h>

#include <Fluxion/Foundation/Assert.h>
#include <Fluxion/Foundation/Defines.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <string.h>

typedef struct FluxionWindowSlot
{
    Window xwindow;
    u32 generation;
    bool inUse;
    bool fullscreen;
    bool cursorVisible;
    Cursor blankCursor; // created lazily the first time the cursor is hidden
} FluxionWindowSlot;

static FluxionAllocator* s_allocator = NULL;
static FluxionEventQueue* s_eventQueue = NULL;
static FluxionWindowSlot* s_slots = NULL;
static usize s_slotCount = 0;
static Display* s_display = NULL;
static i32 s_screen = 0;
static Atom s_wmDeleteWindow;
static Atom s_wmProtocols;
static Atom s_netWmState;
static Atom s_netWmStateFullscreen;

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

// Matches the Windows backend's mouse button ordering (0=left, 1=right,
// 2=middle) instead of X11's native Button1/2/3 = left/middle/right.
static i32 Fluxion_TranslateMouseButton(unsigned int x11Button)
{
    switch (x11Button)
    {
        case Button1: return 0; // left
        case Button3: return 1; // right
        case Button2: return 2; // middle
        case 8:       return 3; // X1 / back (common convention, not X11-standardized)
        case 9:       return 4; // X2 / forward
        default:      return (i32)x11Button;
    }
}

static usize Fluxion_FindSlotByXWindow(Window xwindow)
{
    for (usize i = 0; i < s_slotCount; ++i)
    {
        if (s_slots[i].inUse && s_slots[i].xwindow == xwindow)
        {
            return i;
        }
    }
    return (usize)-1;
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

    s_display = XOpenDisplay(NULL);
    FLUXION_ASSERT_MSG(s_display != NULL, "Fluxion_WindowSystem_Init: XOpenDisplay failed (no DISPLAY?)");
    s_screen = DefaultScreen(s_display);

    s_wmDeleteWindow = XInternAtom(s_display, "WM_DELETE_WINDOW", False);
    s_wmProtocols = XInternAtom(s_display, "WM_PROTOCOLS", False);
    s_netWmState = XInternAtom(s_display, "_NET_WM_STATE", False);
    s_netWmStateFullscreen = XInternAtom(s_display, "_NET_WM_STATE_FULLSCREEN", False);
}

void Fluxion_WindowSystem_Shutdown(void)
{
    for (usize i = 0; i < s_slotCount; ++i)
    {
        if (s_slots[i].inUse)
        {
            if (s_slots[i].blankCursor != None)
            {
                XFreeCursor(s_display, s_slots[i].blankCursor);
            }
            XDestroyWindow(s_display, s_slots[i].xwindow);
        }
    }

    if (s_display)
    {
        XCloseDisplay(s_display);
        s_display = NULL;
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

    Window root = RootWindow(s_display, s_screen);

    XSetWindowAttributes attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
        StructureNotifyMask | FocusChangeMask;

    Window xwindow = XCreateWindow(
        s_display, root, 0, 0, desc->width, desc->height, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWEventMask, &attributes);

    if (!xwindow)
    {
        return invalid;
    }

    XStoreName(s_display, xwindow, desc->title ? desc->title : "");
    XSetWMProtocols(s_display, xwindow, &s_wmDeleteWindow, 1);

    if (!desc->resizable)
    {
        XSizeHints* hints = XAllocSizeHints();
        hints->flags = PMinSize | PMaxSize;
        hints->min_width = hints->max_width = (int)desc->width;
        hints->min_height = hints->max_height = (int)desc->height;
        XSetWMNormalHints(s_display, xwindow, hints);
        XFree(hints);
    }

    XMapWindow(s_display, xwindow);
    XFlush(s_display);

    s_slots[index].xwindow = xwindow;
    s_slots[index].inUse = true;
    s_slots[index].fullscreen = false;
    s_slots[index].cursorVisible = true;
    s_slots[index].blankCursor = None;
    s_slots[index].generation += 1;

    return Fluxion_MakeHandle(index, s_slots[index].generation);
}

void Fluxion_Window_Destroy(FluxionWindowHandle handle)
{
    FluxionWindowSlot* slot = Fluxion_ResolveSlot(handle);
    if (!slot)
    {
        return;
    }

    if (slot->blankCursor != None)
    {
        XFreeCursor(s_display, slot->blankCursor);
        slot->blankCursor = None;
    }
    XDestroyWindow(s_display, slot->xwindow);
    XFlush(s_display);
    slot->xwindow = 0;
    slot->inUse = false;
}

void Fluxion_WindowSystem_PollEvents(void)
{
    if (!s_display)
    {
        return;
    }

    while (XPending(s_display) > 0)
    {
        XEvent xevent;
        XNextEvent(s_display, &xevent);

        usize index = Fluxion_FindSlotByXWindow(xevent.xany.window);
        if (index == (usize)-1)
        {
            continue;
        }

        FluxionEvent event;
        memset(&event, 0, sizeof(event));
        event.window = Fluxion_MakeHandle(index, s_slots[index].generation);

        switch (xevent.type)
        {
            case ClientMessage:
                if (xevent.xclient.message_type == s_wmProtocols &&
                    (Atom)xevent.xclient.data.l[0] == s_wmDeleteWindow)
                {
                    event.type = FLUXION_EVENT_WINDOW_CLOSE_REQUESTED;
                    Fluxion_EventQueue_Push(s_eventQueue, &event);
                }
                break;

            case ConfigureNotify:
                event.type = FLUXION_EVENT_WINDOW_RESIZED;
                event.data.resized.width = (u32)xevent.xconfigure.width;
                event.data.resized.height = (u32)xevent.xconfigure.height;
                Fluxion_EventQueue_Push(s_eventQueue, &event);
                break;

            case FocusIn:
                event.type = FLUXION_EVENT_WINDOW_FOCUS_GAINED;
                Fluxion_EventQueue_Push(s_eventQueue, &event);
                break;

            case FocusOut:
                event.type = FLUXION_EVENT_WINDOW_FOCUS_LOST;
                Fluxion_EventQueue_Push(s_eventQueue, &event);
                break;

            case KeyPress:
                // A raw X11 keycode is hardware/layout-dependent. The
                // KeySym XLookupKeysym resolves it to (XK_a, XK_F1, ...)
                // is the stable, documented, portable-within-X11
                // identifier — that's what Input's keycode translation
                // tables are built against.
                event.type = FLUXION_EVENT_KEY_DOWN;
                event.data.key.keyCode = (i32)XLookupKeysym(&xevent.xkey, 0);
                Fluxion_EventQueue_Push(s_eventQueue, &event);
                break;

            case KeyRelease:
                event.type = FLUXION_EVENT_KEY_UP;
                event.data.key.keyCode = (i32)XLookupKeysym(&xevent.xkey, 0);
                Fluxion_EventQueue_Push(s_eventQueue, &event);
                break;

            case MotionNotify:
                event.type = FLUXION_EVENT_MOUSE_MOVED;
                event.data.mouseMoved.x = xevent.xmotion.x;
                event.data.mouseMoved.y = xevent.xmotion.y;
                Fluxion_EventQueue_Push(s_eventQueue, &event);
                break;

            case ButtonPress:
                if (xevent.xbutton.button == Button4 || xevent.xbutton.button == Button5)
                {
                    event.type = FLUXION_EVENT_MOUSE_SCROLLED;
                    event.data.mouseScroll.deltaY = (xevent.xbutton.button == Button4) ? 1.0f : -1.0f;
                    Fluxion_EventQueue_Push(s_eventQueue, &event);
                }
                else
                {
                    event.type = FLUXION_EVENT_MOUSE_BUTTON_DOWN;
                    event.data.mouseButton.button = Fluxion_TranslateMouseButton(xevent.xbutton.button);
                    event.data.mouseButton.x = xevent.xbutton.x;
                    event.data.mouseButton.y = xevent.xbutton.y;
                    Fluxion_EventQueue_Push(s_eventQueue, &event);
                }
                break;

            case ButtonRelease:
                if (xevent.xbutton.button != Button4 && xevent.xbutton.button != Button5)
                {
                    event.type = FLUXION_EVENT_MOUSE_BUTTON_UP;
                    event.data.mouseButton.button = Fluxion_TranslateMouseButton(xevent.xbutton.button);
                    event.data.mouseButton.x = xevent.xbutton.x;
                    event.data.mouseButton.y = xevent.xbutton.y;
                    Fluxion_EventQueue_Push(s_eventQueue, &event);
                }
                break;

            default:
                break;
        }
    }
}

void Fluxion_Window_SetTitle(FluxionWindowHandle handle, const char* title)
{
    FluxionWindowSlot* slot = Fluxion_ResolveSlot(handle);
    if (!slot)
    {
        return;
    }
    XStoreName(s_display, slot->xwindow, title ? title : "");
    XFlush(s_display);
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

    XWindowAttributes attributes;
    XGetWindowAttributes(s_display, slot->xwindow, &attributes);
    if (outWidth) *outWidth = (u32)attributes.width;
    if (outHeight) *outHeight = (u32)attributes.height;
}

static void Fluxion_SendNetWmStateEvent(Window window, long action, Atom state)
{
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = s_netWmState;
    event.xclient.format = 32;
    event.xclient.data.l[0] = action; // 0 = remove, 1 = add, 2 = toggle
    event.xclient.data.l[1] = (long)state;
    event.xclient.data.l[2] = 0;
    event.xclient.data.l[3] = 1; // source indication: normal application

    XSendEvent(s_display, RootWindow(s_display, s_screen), False,
        SubstructureRedirectMask | SubstructureNotifyMask, &event);
}

void Fluxion_Window_SetFullscreen(FluxionWindowHandle handle, bool fullscreen)
{
    FluxionWindowSlot* slot = Fluxion_ResolveSlot(handle);
    if (!slot || slot->fullscreen == fullscreen)
    {
        return;
    }

    Fluxion_SendNetWmStateEvent(slot->xwindow, fullscreen ? 1 : 0, s_netWmStateFullscreen);
    XFlush(s_display);
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

    if (!visible)
    {
        if (slot->blankCursor == None)
        {
            char data[1] = { 0 };
            Pixmap blankPixmap = XCreateBitmapFromData(s_display, slot->xwindow, data, 1, 1);
            XColor black;
            memset(&black, 0, sizeof(black));
            slot->blankCursor = XCreatePixmapCursor(s_display, blankPixmap, blankPixmap, &black, &black, 0, 0);
            XFreePixmap(s_display, blankPixmap);
        }
        XDefineCursor(s_display, slot->xwindow, slot->blankCursor);
    }
    else
    {
        XUndefineCursor(s_display, slot->xwindow);
    }

    XFlush(s_display);
    slot->cursorVisible = visible;
}

FluxionNativeWindowHandle Fluxion_Window_GetNativeHandle(FluxionWindowHandle handle)
{
    FluxionWindowSlot* slot = Fluxion_ResolveSlot(handle);
    FluxionNativeWindowHandle native;
    native.value = slot ? (void*)(uintptr_t)slot->xwindow : NULL;
    return native;
}

void* Fluxion_WindowSystem_GetNativeDisplayHandle(void)
{
    return (void*)s_display;
}
