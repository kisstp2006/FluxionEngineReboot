// WGL/GLX context bootstrap + GL 1.2+ function-pointer loading. See the
// design note at the top of OpenGLCommon.h for why context creation needs
// a hidden bootstrap window.

#include "OpenGLCommon.h"
#include "OpenGLFunctions.h"

#include <Fluxion/Application/Window/Window.h>
#include <Fluxion/Foundation/Log.h>

#include <cstdio>
#include <cstring>

// --- Function pointer storage ------------------------------------------------

#define FLUXION_GL_DEFINE(type, name) type name = nullptr;
FLUXION_GL_FUNCTIONS(FLUXION_GL_DEFINE)
#undef FLUXION_GL_DEFINE

#if defined(_WIN32)
static PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB_ = nullptr;
static PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB_ = nullptr;

static void* Fluxion_RHIOpenGL_GetProc(const char* name)
{
    void* p = (void*)wglGetProcAddress(name);
    if (p == nullptr || p == (void*)0x1 || p == (void*)0x2 || p == (void*)0x3 || p == (void*)-1)
    {
        HMODULE module = GetModuleHandleA("opengl32.dll");
        p = module != nullptr ? (void*)GetProcAddress(module, name) : nullptr;
    }
    return p;
}
#else
static PFNGLXCREATECONTEXTATTRIBSARBPROC glXCreateContextAttribsARB_ = nullptr;

static void* Fluxion_RHIOpenGL_GetProc(const char* name)
{
    return (void*)glXGetProcAddressARB((const GLubyte*)name);
}
#endif

void Fluxion_RHIOpenGL_LoadFunctions(void)
{
#define FLUXION_GL_LOAD(type, name) name = (type)Fluxion_RHIOpenGL_GetProc(#name);
    FLUXION_GL_FUNCTIONS(FLUXION_GL_LOAD)
#undef FLUXION_GL_LOAD
}

// --- KHR_debug callback (design decision #10) -------------------------------

static void APIENTRY Fluxion_RHIOpenGL_DebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity,
    GLsizei length, const GLchar* message, const void* userParam)
{
    FLUXION_UNUSED(source);
    FLUXION_UNUSED(id);
    FLUXION_UNUSED(length);
    FLUXION_UNUSED(userParam);

    // What kind of message it is decides the level, and only then how
    // severe the driver called it. Severity alone is not enough: drivers
    // rate a performance note as medium, and reporting that as an error
    // says something went wrong when nothing did. Nothing is silenced --
    // every message still comes out, at a level that matches what it is.
    // The point of "the debug output is clean" is that an error line
    // means an error; a log full of errors that are not errors is a log
    // nobody reads.
    if (type == GL_DEBUG_TYPE_ERROR || type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR)
    {
        FLUXION_LOG_ERROR("RHI.OpenGL", "GL debug: %s", message);
        return;
    }
    if (type == GL_DEBUG_TYPE_PERFORMANCE || type == GL_DEBUG_TYPE_PORTABILITY || type == GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR)
    {
        // Never an error however the driver rated it: none of these says
        // the wrong thing was drawn, only that it could have been done
        // better or will stop working one day.
        FLUXION_LOG_WARN("RHI.OpenGL", "GL debug: %s", message);
        return;
    }

    // Anything else -- markers, group push/pop, whatever a driver invents
    // -- has no meaning of its own here, so the driver's own rating is
    // all there is to go on.
    if (severity == GL_DEBUG_SEVERITY_HIGH || severity == GL_DEBUG_SEVERITY_MEDIUM)
    {
        FLUXION_LOG_ERROR("RHI.OpenGL", "GL debug: %s", message);
    }
    else if (severity == GL_DEBUG_SEVERITY_LOW)
    {
        FLUXION_LOG_WARN("RHI.OpenGL", "GL debug: %s", message);
    }
    // GL_DEBUG_SEVERITY_NOTIFICATION is intentionally skipped -- too noisy
    // to be useful at any log level by default.
}

// --- Extension probing -------------------------------------------------------

static bool Fluxion_RHIOpenGL_HasExtension(const char* name)
{
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i)
    {
        const GLubyte* ext = glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (ext != nullptr && std::strcmp((const char*)ext, name) == 0) return true;
    }
    return false;
}

// --- Win32 (WGL) --------------------------------------------------------------

#if defined(_WIN32)

static LRESULT CALLBACK Fluxion_RHIOpenGL_DummyWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

static bool Fluxion_RHIOpenGL_LoadWGLExtensions(void)
{
    // The "dummy context" dance: WGL only exposes wglCreateContextAttribsARB/
    // wglChoosePixelFormatARB once *some* context is current, so a throwaway
    // legacy (1.1) context against a throwaway window is created first, just
    // to load those two entry points.
    WNDCLASSA wc = {};
    wc.lpfnWndProc = Fluxion_RHIOpenGL_DummyWndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "FluxionRHIOpenGLDummyWindow";
    RegisterClassA(&wc);

    HWND dummyHwnd = CreateWindowExA(0, wc.lpszClassName, "", 0, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    if (dummyHwnd == nullptr) return false;

    HDC dummyHdc = GetDC(dummyHwnd);

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;

    int format = ChoosePixelFormat(dummyHdc, &pfd);
    if (format == 0 || !SetPixelFormat(dummyHdc, format, &pfd))
    {
        ReleaseDC(dummyHwnd, dummyHdc);
        DestroyWindow(dummyHwnd);
        return false;
    }

    HGLRC dummyContext = wglCreateContext(dummyHdc);
    if (dummyContext == nullptr)
    {
        ReleaseDC(dummyHwnd, dummyHdc);
        DestroyWindow(dummyHwnd);
        return false;
    }
    wglMakeCurrent(dummyHdc, dummyContext);

    wglCreateContextAttribsARB_ = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
    wglChoosePixelFormatARB_ = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(dummyContext);
    ReleaseDC(dummyHwnd, dummyHdc);
    DestroyWindow(dummyHwnd);

    return wglCreateContextAttribsARB_ != nullptr;
}

bool Fluxion_RHIOpenGL_CreateContext(FluxionRHIOpenGLDevice* deviceState, bool enableValidation)
{
    if (!Fluxion_RHIOpenGL_LoadWGLExtensions()) return false;

    WNDCLASSA wc = {};
    wc.lpfnWndProc = Fluxion_RHIOpenGL_DummyWndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "FluxionRHIOpenGLBootstrapWindow";
    RegisterClassA(&wc);

    deviceState->bootstrapHwnd = CreateWindowExA(0, wc.lpszClassName, "", 0, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    if (deviceState->bootstrapHwnd == nullptr) return false;

    HDC hdc = GetDC(deviceState->bootstrapHwnd);

    int pixelFormatAttribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB, 32,
        WGL_DEPTH_BITS_ARB, 24,
        WGL_STENCIL_BITS_ARB, 8,
        0,
    };

    int pixelFormat = 0;
    UINT numFormats = 0;
    if (!wglChoosePixelFormatARB_(hdc, pixelFormatAttribs, nullptr, 1, &pixelFormat, &numFormats) || numFormats == 0)
    {
        ReleaseDC(deviceState->bootstrapHwnd, hdc);
        DestroyWindow(deviceState->bootstrapHwnd);
        deviceState->bootstrapHwnd = nullptr;
        return false;
    }

    PIXELFORMATDESCRIPTOR pfd = {};
    DescribePixelFormat(hdc, pixelFormat, sizeof(pfd), &pfd);
    if (!SetPixelFormat(hdc, pixelFormat, &pfd))
    {
        ReleaseDC(deviceState->bootstrapHwnd, hdc);
        DestroyWindow(deviceState->bootstrapHwnd);
        deviceState->bootstrapHwnd = nullptr;
        return false;
    }
    deviceState->pixelFormat = pixelFormat;

    int contextFlags = enableValidation ? WGL_CONTEXT_DEBUG_BIT_ARB : 0;
    int contextAttribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
        WGL_CONTEXT_MINOR_VERSION_ARB, 5,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        WGL_CONTEXT_FLAGS_ARB, contextFlags,
        0,
    };

    deviceState->context = wglCreateContextAttribsARB_(hdc, nullptr, contextAttribs);
    if (deviceState->context == nullptr)
    {
        ReleaseDC(deviceState->bootstrapHwnd, hdc);
        DestroyWindow(deviceState->bootstrapHwnd);
        deviceState->bootstrapHwnd = nullptr;
        return false;
    }

    if (!wglMakeCurrent(hdc, deviceState->context))
    {
        wglDeleteContext(deviceState->context);
        deviceState->context = nullptr;
        ReleaseDC(deviceState->bootstrapHwnd, hdc);
        DestroyWindow(deviceState->bootstrapHwnd);
        deviceState->bootstrapHwnd = nullptr;
        return false;
    }
    deviceState->currentHdc = hdc;
    deviceState->contextValid = true;

    Fluxion_RHIOpenGL_LoadFunctions();

    if (enableValidation && glDebugMessageCallback != nullptr)
    {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(Fluxion_RHIOpenGL_DebugCallback, nullptr);
        deviceState->debugEnabled = true;
    }

    deviceState->hasAnisotropicFiltering = Fluxion_RHIOpenGL_HasExtension("GL_ARB_texture_filter_anisotropic")
        || Fluxion_RHIOpenGL_HasExtension("GL_EXT_texture_filter_anisotropic");
    GLint programBinaryFormats = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &programBinaryFormats);
    deviceState->hasProgramBinary = programBinaryFormats > 0;

    return true;
}

void Fluxion_RHIOpenGL_DestroyContext(FluxionRHIOpenGLDevice* deviceState)
{
    if (deviceState->context != nullptr)
    {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(deviceState->context);
        deviceState->context = nullptr;
    }
    if (deviceState->currentHdc != nullptr && deviceState->bootstrapHwnd != nullptr)
    {
        ReleaseDC(deviceState->bootstrapHwnd, deviceState->currentHdc);
        deviceState->currentHdc = nullptr;
    }
    if (deviceState->bootstrapHwnd != nullptr)
    {
        DestroyWindow(deviceState->bootstrapHwnd);
        deviceState->bootstrapHwnd = nullptr;
    }
    deviceState->contextValid = false;
}

bool Fluxion_RHIOpenGL_MakeCurrent(FluxionRHIOpenGLDevice* deviceState, HDC hdc)
{
    if (deviceState == nullptr || deviceState->context == nullptr) return false;
    // A real target window must use the SAME pixel format the context was
    // created with -- SetPixelFormat may only be called once per HWND, so
    // the caller (OpenGLCommandList.cpp's swapchain code) is responsible
    // for calling SetPixelFormat(hdc, deviceState->pixelFormat, ...) on a
    // freshly created real window before the first MakeCurrent against it.
    if (!wglMakeCurrent(hdc, deviceState->context)) return false;
    deviceState->currentHdc = hdc;
    return true;
}

#else // Linux (GLX) -----------------------------------------------------------

static bool Fluxion_RHIOpenGL_LoadGLXExtensions(void)
{
    glXCreateContextAttribsARB_ = (PFNGLXCREATECONTEXTATTRIBSARBPROC)glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");
    return glXCreateContextAttribsARB_ != nullptr;
}

bool Fluxion_RHIOpenGL_CreateContext(FluxionRHIOpenGLDevice* deviceState, bool enableValidation)
{
    deviceState->display = (Display*)Fluxion_WindowSystem_GetNativeDisplayHandle();
    if (deviceState->display == nullptr) return false;

    if (!Fluxion_RHIOpenGL_LoadGLXExtensions()) return false;

    int screen = DefaultScreen(deviceState->display);

    int fbAttribs[] = {
        GLX_X_RENDERABLE, True,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8,
        GLX_DEPTH_SIZE, 24,
        GLX_STENCIL_SIZE, 8,
        GLX_DOUBLEBUFFER, True,
        None,
    };

    int fbCount = 0;
    GLXFBConfig* fbConfigs = glXChooseFBConfig(deviceState->display, screen, fbAttribs, &fbCount);
    if (fbConfigs == nullptr || fbCount == 0) return false;
    deviceState->fbConfig = fbConfigs[0];
    XFree(fbConfigs);

    XVisualInfo* visualInfo = glXGetVisualFromFBConfig(deviceState->display, deviceState->fbConfig);
    if (visualInfo == nullptr) return false;

    ::Window root = RootWindow(deviceState->display, screen);
    XSetWindowAttributes windowAttribs = {};
    windowAttribs.colormap = XCreateColormap(deviceState->display, root, visualInfo->visual, AllocNone);
    windowAttribs.event_mask = 0;
    deviceState->bootstrapXWindow = XCreateWindow(deviceState->display, root, 0, 0, 1, 1, 0, visualInfo->depth,
        InputOutput, visualInfo->visual, CWColormap | CWEventMask, &windowAttribs);
    XFree(visualInfo);
    if (deviceState->bootstrapXWindow == 0) return false;

    int contextFlags = enableValidation ? GLX_CONTEXT_DEBUG_BIT_ARB : 0;
    int contextAttribs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 4,
        GLX_CONTEXT_MINOR_VERSION_ARB, 5,
        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        GLX_CONTEXT_FLAGS_ARB, contextFlags,
        None,
    };

    deviceState->context = glXCreateContextAttribsARB_(deviceState->display, deviceState->fbConfig, nullptr, True, contextAttribs);
    if (deviceState->context == nullptr)
    {
        XDestroyWindow(deviceState->display, deviceState->bootstrapXWindow);
        deviceState->bootstrapXWindow = 0;
        return false;
    }

    if (!glXMakeCurrent(deviceState->display, deviceState->bootstrapXWindow, deviceState->context))
    {
        glXDestroyContext(deviceState->display, deviceState->context);
        deviceState->context = nullptr;
        XDestroyWindow(deviceState->display, deviceState->bootstrapXWindow);
        deviceState->bootstrapXWindow = 0;
        return false;
    }
    deviceState->currentXWindow = deviceState->bootstrapXWindow;
    deviceState->contextValid = true;

    Fluxion_RHIOpenGL_LoadFunctions();

    if (enableValidation && glDebugMessageCallback != nullptr)
    {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(Fluxion_RHIOpenGL_DebugCallback, nullptr);
        deviceState->debugEnabled = true;
    }

    deviceState->hasAnisotropicFiltering = Fluxion_RHIOpenGL_HasExtension("GL_ARB_texture_filter_anisotropic")
        || Fluxion_RHIOpenGL_HasExtension("GL_EXT_texture_filter_anisotropic");
    GLint programBinaryFormats = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &programBinaryFormats);
    deviceState->hasProgramBinary = programBinaryFormats > 0;

    return true;
}

void Fluxion_RHIOpenGL_DestroyContext(FluxionRHIOpenGLDevice* deviceState)
{
    if (deviceState->display != nullptr && deviceState->context != nullptr)
    {
        glXMakeCurrent(deviceState->display, 0, nullptr);
        glXDestroyContext(deviceState->display, deviceState->context);
        deviceState->context = nullptr;
    }
    if (deviceState->display != nullptr && deviceState->bootstrapXWindow != 0)
    {
        XDestroyWindow(deviceState->display, deviceState->bootstrapXWindow);
        deviceState->bootstrapXWindow = 0;
    }
    deviceState->contextValid = false;
}

bool Fluxion_RHIOpenGL_MakeCurrent(FluxionRHIOpenGLDevice* deviceState, ::Window xWindow)
{
    if (deviceState == nullptr || deviceState->context == nullptr) return false;
    if (!glXMakeCurrent(deviceState->display, xWindow, deviceState->context)) return false;
    deviceState->currentXWindow = xWindow;
    return true;
}

#endif
