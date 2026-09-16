#define GL_GLEXT_PROTOTYPES
#define DEFAULT_WINDOW_TITLE "Lindbergh Game"
#include <glad/gl.h>

#include <windows.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "fpsLimiter.h"
#include "gui.h"
#include "internalRes.h"
#include "msaa.h"
#include "gpuVendor.h"
#include "../hooks/glx.h"
#include "../config/config.h"
#include "../devices/jvs.h"
#include "../log/log.h"

HWND g_Window = NULL;
HGLRC g_GLContext = NULL;
HDC g_WindowDC = NULL;

int glutInitialized = 0;

HDC fontHdc = NULL;
HFONT font = NULL;

void quitGraphics();

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            if (wParam == VK_MENU)
                return 0;
            break;
        case WM_CLOSE:
            quitGraphics();
            return 0;
        
        case WM_SIZE:
            break;
        
        case WM_DISPLAYCHANGE:
            break;
            
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

static void* getGLProcAddress(const char* procName)
{
    static HMODULE hOpenGL32 = NULL;
    static void* (*wglGetProcAddressPtr)(const char*) = NULL;

    if (!hOpenGL32)
    {
        hOpenGL32 = LoadLibrary("opengl32.dll");
        if (!hOpenGL32)
        {
            log_error(plfGUI, "Failed to load opengl32.dll! Error: %lu\n", GetLastError());
            return NULL;
        }

        wglGetProcAddressPtr = (void* (*)(const char*))(void*)GetProcAddress(hOpenGL32, "wglGetProcAddress");
        if (!wglGetProcAddressPtr)
        {
            log_error(plfGUI, "Failed to get wglGetProcAddress! Error: %lu\n", GetLastError());
            FreeLibrary(hOpenGL32);
            hOpenGL32 = NULL;
            return NULL;
        }
    }

    void* proc = wglGetProcAddressPtr(procName);
    if (proc)
        return proc;
    
    return GetProcAddress(hOpenGL32, procName);
}

// --- Real GLX FBConfig -> WGL pixel format translation ----------------------
// wglChoosePixelFormatARB is only available once a GL context exists. So we
// spin up a throwaway hidden window + context purely to grab the extension
// pointer, then query the format against the real window DC.
int choosePixelFormatFor(HDC hdc, const struct __GLXFBConfigRec *cfg)
{
    int pixelFormat = 0;
    PIXELFORMATDESCRIPTOR pfd;

    {
        static int bootClass = 0;
        if (!bootClass)
        {
            WNDCLASS wc = {0};
            wc.style = CS_OWNDC;
            wc.lpfnWndProc = DefWindowProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = "TempGLBootstrap";
            RegisterClass(&wc);
            bootClass = 1;
        }

        HWND thw = CreateWindowEx(0, "TempGLBootstrap", "", WS_POPUP, 0, 0, 8, 8,
                                  NULL, NULL, GetModuleHandle(NULL), NULL);
        if (thw)
        {
            HDC thdc = GetDC(thw);
            PIXELFORMATDESCRIPTOR tpfd = {
                sizeof(PIXELFORMATDESCRIPTOR), 1,
                PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
                PFD_TYPE_RGBA, 32, 8, 0, 8, 0, 8, 0, 8, 0,
                0, 0, 0, 0, 0, 24, 8, 0, PFD_MAIN_PLANE, 0, 0, 0, 0};
            int tpf = ChoosePixelFormat(thdc, &tpfd);
            if (tpf)
                SetPixelFormat(thdc, tpf, &tpfd);
            HGLRC tctx = wglCreateContext(thdc);
            if (tctx)
            {
                wglMakeCurrent(thdc, tctx);
                PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB =
                    (PFNWGLCHOOSEPIXELFORMATARBPROC)(void *)wglGetProcAddress("wglChoosePixelFormatARB");
                /* wglGetPixelFormatAttribivARB: lets us read back the real
                 * WGL_SAMPLE_BUFFERS_ARB / WGL_SAMPLES_ARB of a chosen format,
                 * so we can tell whether MSAA actually applied. */
                typedef BOOL (WINAPI *PFNWGLGETPIXELFORMATATTRIBIVARBPROC)(HDC, int, int, UINT, const int *, int *);
                (void)sizeof(PFNWGLGETPIXELFORMATATTRIBIVARBPROC);
                if (wglChoosePixelFormatARB)
                {
                    int attrs[64];
                    int n = 0;
                    attrs[n++] = WGL_DRAW_TO_WINDOW_ARB; attrs[n++] = TRUE;
                    attrs[n++] = WGL_SUPPORT_GL_ARB;     attrs[n++] = TRUE;
                    attrs[n++] = WGL_PIXEL_TYPE_ARB;     attrs[n++] = WGL_TYPE_RGBA_ARB;
                    attrs[n++] = WGL_DOUBLE_BUFFER_ARB;  attrs[n++] = cfg->doublebuffer ? TRUE : FALSE;
                    attrs[n++] = WGL_STEREO_ARB;         attrs[n++] = cfg->stereo ? TRUE : FALSE;
                    if (cfg->red_size)    { attrs[n++] = WGL_RED_BITS_ARB;    attrs[n++] = cfg->red_size; }
                    if (cfg->green_size)  { attrs[n++] = WGL_GREEN_BITS_ARB;  attrs[n++] = cfg->green_size; }
                    if (cfg->blue_size)   { attrs[n++] = WGL_BLUE_BITS_ARB;   attrs[n++] = cfg->blue_size; }
                    if (cfg->alpha_size)  { attrs[n++] = WGL_ALPHA_BITS_ARB;  attrs[n++] = cfg->alpha_size; }
                    if (cfg->depth_size)  { attrs[n++] = WGL_DEPTH_BITS_ARB;  attrs[n++] = cfg->depth_size; }
                    if (cfg->stencil_size){ attrs[n++] = WGL_STENCIL_BITS_ARB;attrs[n++] = cfg->stencil_size; }
                    if (cfg->aux_buffers) { attrs[n++] = WGL_AUX_BUFFERS_ARB; attrs[n++] = cfg->aux_buffers; }
                    
                    attrs[n++] = 0; attrs[n++] = 0;
                    UINT numFormats = 0;
                    wglChoosePixelFormatARB(hdc, attrs, NULL, 1, &pixelFormat, &numFormats);
                    if (numFormats < 1)
                        pixelFormat = 0;
                }
                wglMakeCurrent(NULL, NULL);
                wglDeleteContext(tctx);
            }
            ReleaseDC(thw, thdc);
            DestroyWindow(thw);
        }
    }

    if (pixelFormat == 0)
    {
        // Fallback: no ARB selector -> basic ChoosePixelFormat.
        pfd = (PIXELFORMATDESCRIPTOR){
            sizeof(PIXELFORMATDESCRIPTOR), 1,
            PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | (cfg->doublebuffer ? PFD_DOUBLEBUFFER : 0),
            PFD_TYPE_RGBA, 32, 8, 0, 8, 0, 8, 0, 8, 0,
            0, 0, 0, 0, 0,
            cfg->depth_size ? cfg->depth_size : 24,
            cfg->stencil_size ? cfg->stencil_size : 8,
            0, PFD_MAIN_PLANE, 0, 0, 0, 0};
        pixelFormat = ChoosePixelFormat(hdc, &pfd);
    }
    else
    {
        DescribePixelFormat(hdc, pixelFormat, sizeof(pfd), &pfd);
    }

    if (!pixelFormat || !SetPixelFormat(hdc, pixelFormat, &pfd))
    {
        log_error(plfGUI, "Failed to set pixel format! Error: %lu\n", GetLastError());
        return -1;
    }

    log_debug(plfGUI, "Chosen pixel format=%d (color=%d depth=%d stencil=%d)",
             pixelFormat,
             cfg->red_size + cfg->green_size + cfg->blue_size + cfg->alpha_size,
             cfg->depth_size, cfg->stencil_size);
    return 0;
}

void startGraphics()
{
    if (g_Window)
        return;

    DEVMODE dm;
    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(dm);
    EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm);
    
    WNDCLASS wc = {0};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = DEFAULT_WINDOW_TITLE;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    if (!RegisterClass(&wc))
    {
        log_error(plfGUI, "Failed to register window class\n");
        exit(EXIT_FAILURE);
    }
    
    DWORD style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    if (Config.display.borderless)
        style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_POPUP;
    
    RECT windowRect = {0, 0, Config.display.width, Config.display.height};
    AdjustWindowRect(&windowRect, style, FALSE);
    
    g_Window = CreateWindowEx(
        0,
        DEFAULT_WINDOW_TITLE,
        DEFAULT_WINDOW_TITLE,
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );
    
    ImmAssociateContext(g_Window, NULL);
    if (!g_Window)
    {
        log_error(plfGUI, "Window could not be created! Error: %lu\n", GetLastError());
        exit(EXIT_FAILURE);
    }
    
    HDC hdc = GetDC(g_Window);
    
    // Honour the GLX framebuffer config the game requested (set by
    // hook_GlxChooseFBConfig / GlxChooseFBConfigSGIX / hook_GlxChooseVisual).
    // Falls back to a 32-bit RGBA double-buffered 24/8 config when the game
    // never called any of those.
    if (choosePixelFormatFor(hdc, &g_DesiredFBConfig) < 0)
    {
        ReleaseDC(g_Window, hdc);
        DestroyWindow(g_Window);
        g_Window = NULL;
        exit(EXIT_FAILURE);
    }
    
    g_GLContext = wglCreateContext(hdc);
    if (!g_GLContext)
    {
        log_error(plfGUI, "OpenGL context could not be created! Error: %lu\n", GetLastError());
        ReleaseDC(g_Window, hdc);
        DestroyWindow(g_Window);
        g_Window = NULL;
        exit(EXIT_FAILURE);
    }
    
    if (!wglMakeCurrent(hdc, g_GLContext))
    {
        log_error(plfGUI, "Failed to make OpenGL context current! Error: %lu\n", GetLastError());
        wglDeleteContext(g_GLContext);
        ReleaseDC(g_Window, hdc);
        DestroyWindow(g_Window);
        g_Window = NULL;
        exit(EXIT_FAILURE);
    }
    
    if (!gladLoadGL((GLADloadfunc)getGLProcAddress))
    {
        log_error(plfGUI, "Failed to initialize GLAD.\n");
        exit(EXIT_FAILURE);
    }

    {
        const char *vendor = (const char *)glGetString(GL_VENDOR);
        const char *renderer = (const char *)glGetString(GL_RENDERER);
        setGPUVendorFromOpenGL(vendor, renderer);
        log_info(plfGUI, "OpenGL vendor: %s; renderer: %s",
                 vendor != NULL ? vendor : "Unknown",
                 renderer != NULL ? renderer : "Unknown");
    }

    /* GL is ready: clamp MSAA samples to the hardware's GL_MAX_SAMPLES. */
    msPostGLInit();

    if (Config.display.fullscreen)
    {
        SetWindowLongPtr(g_Window, GWL_STYLE, WS_POPUP);
        SetWindowPos(g_Window, HWND_TOP, 0, 0, dm.dmPelsWidth, dm.dmPelsHeight, SWP_SHOWWINDOW);
    }

    g_WindowDC = GetDC(g_Window);

    ShowWindow(g_Window, SW_SHOW);

    hook_GlxSwapIntervalSGI(0);
    
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    SwapBuffers(hdc);
    
    ReleaseDC(g_Window, hdc);
    
}

void createWindow()
{
    if (!g_Window)
        startGraphics();
}

void ensureGraphicsContext(void)
{
    if (!g_GLContext)
        startGraphics();
}

HWND getWindow()
{
    return g_Window;
}

HGLRC getGLContext()
{
    return g_GLContext;
}

int makeGLCurrent(HWND win, HGLRC ctx)
{
    HDC hdc = NULL;
    bool release = false;

    if (win == g_Window)
    {
        hdc = g_WindowDC;
    }
    else if (win)
    {
        hdc = GetDC(win);
        release = true;
    }

    if (!wglMakeCurrent(hdc, ctx))
        return -1;

    return 0;
}

void quitGraphics()
{
    if (font)
        DeleteObject(font);
    
    if (fontHdc)
        DeleteDC(fontHdc);

    if (g_GLContext)
    {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(g_GLContext);
    }

    if (g_WindowDC)
        ReleaseDC(g_Window, g_WindowDC);

    if (g_Window)
        DestroyWindow(g_Window);
    
    exit(0);
}

void processEvents()
{
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}
