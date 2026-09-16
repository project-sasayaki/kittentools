
#include <windows.h>
#include "../elfLoader/glHooks.h"
#include "../config/config.h"
#include "../graphics/fpsLimiter.h"
#include "../graphics/gui.h"

GLuint msGetSinkFBO(void);
void msOnSwap(void);
void irOnSwap(GLuint);
#include "../log/log.h"
#include <math.h>
#include <stdlib.h>
#include <GL/glu.h>
#include "glx.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define WIN32_LEAN_AND_MEAN

#ifndef WGL_SWAP_MAIN_PLANE
#define WGL_SWAP_MAIN_PLANE 1
#endif

#define GLX_PBUFFER_HEIGHT 0x8040
#define GLX_PBUFFER_WIDTH 0x8041
#define GLX_LARGEST_PBUFFER 0x801C
#define GLX_PRESERVED_CONTENTS 0x801B

HWND g_pbufWindow = NULL;

const char* getErrorString()
{
    static char errorBuffer[256];
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  errorBuffer, sizeof(errorBuffer), NULL);
    return errorBuffer;
}


// --------------------------------------------------------------------------
// Internal Structure Definitions
// --------------------------------------------------------------------------
struct __GLXFBConfigRec g_DesiredFBConfig = {
    .doublebuffer = 1,
    .red_size = 8, .green_size = 8, .blue_size = 8, .alpha_size = 8,
    .depth_size = 24, .stencil_size = 8,
    .drawable_type = GLX_WINDOW_BIT,
    .render_type = GLX_RGBA_BIT,
};

// ==========================================================================
//   GLX Implementation
// ==========================================================================

static void applyGlxAttribs(struct __GLXFBConfigRec *cfg, const int *attrib_list)
{
    if (!attrib_list)
        return;
    for (int i = 0; attrib_list[i] != 0; i += 2)
    {
        int attr = attrib_list[i];
        int val = attrib_list[i + 1];
        switch (attr)
        {
            case GLX_DOUBLEBUFFER:   cfg->doublebuffer = val; break;
            case GLX_STEREO:         cfg->stereo = val; break;
            case GLX_RED_SIZE:       cfg->red_size = val; break;
            case GLX_GREEN_SIZE:     cfg->green_size = val; break;
            case GLX_BLUE_SIZE:      cfg->blue_size = val; break;
            case GLX_ALPHA_SIZE:     cfg->alpha_size = val; break;
            case GLX_DEPTH_SIZE:     cfg->depth_size = val; break;
            case GLX_STENCIL_SIZE:   cfg->stencil_size = val; break;
            case GLX_AUX_BUFFERS:    cfg->aux_buffers = val; break;
            case GLX_BUFFER_SIZE:    cfg->buffer_size = val; break;
            case GLX_DRAWABLE_TYPE:  cfg->drawable_type = val; break;
            case GLX_RENDER_TYPE:    cfg->render_type = val; break;
            case GLX_X_RENDERABLE:   cfg->x_renderable = val; break;
            case GLX_RGBA:           cfg->render_type = GLX_RGBA_BIT; break;
            case GLX_USE_GL:         break; /* always true */
            default: break;
        }
    }
}

XVisualInfo *hook_GlxChooseVisual(Display *dpy, int screen, int *attribList)
{
    applyGlxAttribs(&g_DesiredFBConfig, attribList);

    XVisualInfo *vi = (XVisualInfo *)calloc(1, sizeof(XVisualInfo));
    if (vi)
    {
        vi->visual = (void *)1;
        vi->visualid = 1;
        vi->screen = screen;
        vi->depth = 24;
        vi->class_type = 4; // TrueColor
        vi->bits_per_rgb = 8;
        vi->red_mask = 0xFF0000;
        vi->green_mask = 0x00FF00;
        vi->blue_mask = 0x0000FF;
    }
    return vi;
}

GLXContext hook_GlxCreateContext(Display *dpy, XVisualInfo *vis, GLXContext shareList, bool direct)
{
    log_debug(plfBridges,
              "glXCreateContext(vis=%p, "
              "share=%p, direct=%d)",
              vis, shareList, direct);

    ensureGraphicsContext();

    if (!g_GLContext)
        return NULL;

    // If graphics module already manages the context, we return it.
    if (!g_GLContext)
    {
        log_error(plfGUI, "glXCreateContext: GL Context not available.");
        return NULL;
    }

    log_info(plfGUI, "Created OpenGL Context: %p", g_GLContext);
    return (GLXContext)g_GLContext;
}

void hook_GlxDestroyContext(Display *dpy, GLXContext ctx)
{
    log_debug(plfBridges, "glXDestroyContext(%p)", ctx);
    if (ctx)
    {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext((HGLRC)g_GLContext);
    }
}

bool hook_GlxMakeContextCurrent(Display *dpy, GLXDrawable drawable, GLXDrawable read, GLXContext ctx)
{
    if (drawable != read)
    {
        log_error(plfGUI, "Read and Draw drawables are different.\n");
    }

    HWND target_window = NULL;

    if (drawable == 0)
    { // Unbind context
        if (makeGLCurrent(NULL, NULL) < 0)
        {
            log_error(plfGUI, "glXMakeContextCurrent failed to unbind context: %s\n", getErrorString());
            return 0;
        }
        return 1;
    }
    else if (drawable == 1)
    {
        target_window = g_Window;
    }
    else
    {
        target_window = (HWND)drawable;
    }

    if (!target_window)
    {
        log_error(plfGUI, " drawable %p -> NULL window\n", (void *)drawable);
        return 0;
    }

    if (makeGLCurrent(target_window, (HGLRC)ctx) < 0)
    {
        log_error(plfGUI, "glXMakeCurrent failed for window %p context %p: %s\n", (void *)target_window, (void *)ctx, getErrorString());
        return 0;
    }

    return 1;
}

bool hook_GlxMakeCurrent(Display *dpy, GLXDrawable drawable, GLXContext ctx)
{
    return hook_GlxMakeContextCurrent(NULL, drawable, drawable, ctx);
}

bool hook_GlxQueryVersion(Display *dpy, int *major, int *minor)
{
    if (major)
        *major = 1;
    if (minor)
        *minor = 4;
    return true;
}

void *hook_GlxGetProcAddress(const char *procName)
{
    void *proc = NULL;

    proc = GLHooks_GetProcAddress(procName);
    if (proc)
        return proc;

    log_debug(plfBridges, "GLXBridge::GetProcAddress: Missing wrapper for '%s'", procName);
    return NULL;
}

const char *hook_GlxQueryExtensionsString(Display *dpy, int screen)
{
    return "";
}
const char *hook_GlxQueryServerString(Display *dpy, int screen, int name)
{
    return "";
}
const char *hook_GlxGetClientString(Display *dpy, int name)
{
    return "";
}

extern Display *g_Display;

Display *hook_GlxGetCurrentDisplay()
{
    if (!g_Display)
        g_Display = (Display *)calloc(1, sizeof(Display));
    return g_Display;
}

GLXContext hook_GlxGetCurrentContext()
{
    return (GLXContext)g_GLContext;
}

GLXDrawable hook_GlxGetCurrentDrawable()
{
    HGLRC currentContext = wglGetCurrentContext();
    if (currentContext == (HGLRC)g_GLContext)
        return 1;
    return (GLXDrawable)g_Window;
}

int hook_GlxIsDirect(Display *dpy, GLXContext ctx)
{
    return 1;
}

int hook_GlxGetConfig(Display *dpy, XVisualInfo *vis, int attrib, int *value)
{
    if (value)
        *value = 0;
    return 0;
}

GLXFBConfig *hook_GlxChooseFBConfig(Display *dpy, int screen, int *attrib_list, int *nelements)
{
    struct __GLXFBConfigRec cfg = g_DesiredFBConfig;
    applyGlxAttribs(&cfg, attrib_list);

    g_DesiredFBConfig = cfg;

    struct __GLXFBConfigRec *pcfg = (struct __GLXFBConfigRec *)malloc(sizeof(*pcfg));
    if (pcfg)
        *pcfg = cfg;

    GLXFBConfig *configs = (GLXFBConfig *)malloc(sizeof(GLXFBConfig) * 2);
    if (configs)
    {
        configs[0] = pcfg;
        configs[1] = NULL;
    }
    if (nelements)
        *nelements = 1;
    return configs;
}

int hook_GlxSwapIntervalSGI(int interval)
{
    typedef BOOL (WINAPI *PFNWGLSWAPINTERVALEXTPROC)(int);
    PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)(void*)wglGetProcAddress("wglSwapIntervalEXT");
    if (wglSwapIntervalEXT)
        return wglSwapIntervalEXT(0);
    return 0;
}

// --- SGIX Extensions ---

GLXFBConfig *hook_GlxChooseFBConfigSGIX(Display *dpy, int screen, int *attrib_list, int *nelements)
{
    return hook_GlxChooseFBConfig(dpy, screen, attrib_list, nelements);
}

int hook_GlxGetFBConfigAttribSGIX(Display *dpy, GLXFBConfig config, int attribute, int *value)
{
    struct __GLXFBConfigRec *cfg = (struct __GLXFBConfigRec *)config;
    if (!value)
        return -1;
    if (!cfg)
    {
        *value = 0;
        return 0;
    }
    switch (attribute)
    {
        case GLX_DOUBLEBUFFER:   *value = cfg->doublebuffer; break;
        case GLX_STEREO:         *value = cfg->stereo; break;
        case GLX_RED_SIZE:       *value = cfg->red_size; break;
        case GLX_GREEN_SIZE:     *value = cfg->green_size; break;
        case GLX_BLUE_SIZE:      *value = cfg->blue_size; break;
        case GLX_ALPHA_SIZE:     *value = cfg->alpha_size; break;
        case GLX_DEPTH_SIZE:     *value = cfg->depth_size; break;
        case GLX_STENCIL_SIZE:   *value = cfg->stencil_size; break;
        case GLX_AUX_BUFFERS:    *value = cfg->aux_buffers; break;
        case GLX_BUFFER_SIZE:    *value = (cfg->red_size + cfg->green_size + cfg->blue_size + cfg->alpha_size); break;
        case GLX_DRAWABLE_TYPE:  *value = cfg->drawable_type; break;
        case GLX_RENDER_TYPE:    *value = cfg->render_type; break;
        case GLX_VISUAL_ID:      *value = cfg->visual_id; break;
        case GLX_FBCONFIG_ID:    *value = 1; break;
        case GLX_X_RENDERABLE:   *value = cfg->x_renderable; break;
        default:                 *value = 0; break;
    }
    return 0;
}

XVisualInfo *hook_GlxGetVisualFromFBConfig(Display *dpy, GLXFBConfig config)
{
    struct __GLXFBConfigRec *cfg = (struct __GLXFBConfigRec *)config;
    XVisualInfo *vi = (XVisualInfo *)calloc(1, sizeof(XVisualInfo));
    if (vi)
    {
        vi->visual = (void *)1;
        vi->visualid = 1;
        vi->screen = 0;
        int depth = cfg ? (cfg->red_size + cfg->green_size + cfg->blue_size + cfg->alpha_size) : 24;
        if (depth <= 0)
            depth = 24;
        vi->depth = depth;
        vi->class_type = 4; // TrueColor
        vi->bits_per_rgb = 8;
        vi->red_mask = 0xFF0000;
        vi->green_mask = 0x00FF00;
        vi->blue_mask = 0x0000FF;
    }
    return vi;
}

GLXPbuffer hook_GlxCreatePbuffer(Display *dpy, GLXFBConfig config, const int *attrib_list)
{
    int width = 0;
    int height = 0;

    if (attrib_list)
    {
        for (int i = 0; attrib_list[i] != 0; i += 2)
        {
            if (attrib_list[i] == GLX_PBUFFER_WIDTH)
            {
                width = attrib_list[i + 1];
            }
            if (attrib_list[i] == GLX_PBUFFER_HEIGHT)
            {
                height = attrib_list[i + 1];
            }
        }
    }

    WNDCLASS wc = {0};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "FakePbufferWindowClass";
    RegisterClass(&wc);
    
    g_pbufWindow = CreateWindowEx(
        0,
        "FakePbufferWindowClass",
        "Fake Pbuffer",
        WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, width, height,
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (!g_pbufWindow)
    {
        log_error(plfGUI, "Failed to create hidden window for Pbuffer: %s\n", getErrorString());
        return 0;
    }

    HDC hdc = GetDC(g_pbufWindow);
    PIXELFORMATDESCRIPTOR pfd = {
        sizeof(PIXELFORMATDESCRIPTOR),
        1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        PFD_TYPE_RGBA,
        32,
        8, 0, 8, 0, 8, 0, 8, 0,
        0, 0, 0, 0, 0,
        24,
        8,
        0,
        PFD_MAIN_PLANE,
        0,
        0, 0, 0
    };
    int pixelFormat = ChoosePixelFormat(hdc, &pfd);
    SetPixelFormat(hdc, pixelFormat, &pfd);
    ReleaseDC(g_pbufWindow, hdc);

    return (GLXPbuffer)g_pbufWindow;
}

void hook_GlxDestroyPbuffer(Display *dpy, GLXPbuffer pbuf)
{
    if (pbuf)
        DestroyWindow((HWND)pbuf);
}

void hook_GlxDestroyGLXPbufferSGIX(Display *dpy, GLXPbuffer pbuf)
{
    hook_GlxDestroyPbuffer(dpy, pbuf);
}

GLXPbuffer hook_GlxCreateGLXPbufferSGIX(Display *dpy, GLXFBConfig config, int width, int height, int *attrib_list)
{
    int pbufferAttribs[] = {
        GLX_PBUFFER_WIDTH, width, GLX_PBUFFER_HEIGHT, height, GLX_PRESERVED_CONTENTS, true, GLX_LARGEST_PBUFFER, true, 0};
    return hook_GlxCreatePbuffer(dpy, config, pbufferAttribs);
}

GLXContext hook_GlxCreateNewContext(Display *dpy, GLXFBConfig config, int render_type, GLXContext share_list, bool direct)
{

    if (!g_Window)
    {
        log_error(plfGUI, "No window available for context creation");
        return NULL;
    }

    HDC hdc = GetDC(g_Window);
    
    HGLRC hShareContext = share_list != NULL ? (HGLRC)share_list : NULL;

    HGLRC ctx = wglCreateContext(hdc);
    if (!ctx)
    {
        log_error(plfGUI, "Failed to create GL context: %s", getErrorString());
        ReleaseDC(g_Window, hdc);
        return NULL;
    }

    if (hShareContext)
    {
        HGLRC cur = wglGetCurrentContext();
        HDC  curDC = wglGetCurrentDC();
        if (cur != NULL)
            wglMakeCurrent(NULL, NULL);
        /* Share lists unconditionally: wglShareLists requires both contexts to
         * be non-current, which the unbind above guarantees. Skipping it when
         * nothing was current left the new context without the engine's
         * textures (black rendering in the second context). */
        if (!wglShareLists(hShareContext, ctx))
            log_error(plfGUI, "wglShareLists failed: %s", getErrorString());
        if (cur != NULL)
            wglMakeCurrent(curDC, cur);
    }

    ReleaseDC(g_Window, hdc);
    log_info(plfGUI, "Created GL context: %p (shared=%d)", ctx,
             hShareContext ? 1 : 0);
    return (GLXContext)ctx;
}

GLXContext hook_GlxCreateContextWithConfigSGIX(Display *dpy, GLXFBConfig config, int render_type, GLXContext share_list, bool direct)
{
    return hook_GlxCreateNewContext(dpy, config, render_type, share_list, direct);
}

void hook_GlxSwapBuffers(Display *dpy, GLXDrawable drawable)
{
    processEvents();

    irOnSwap(msGetSinkFBO()); /* guarantee the sink/composite FBO is scaled */
    msOnSwap();               /* resolve the sink FBO if the engine presented it dirty */
    fpsLimiter();
    SwapBuffers(g_WindowDC);
}

void hook_gluPerspective(double fovy, double aspect, double zNear, double zFar)
{
    return gluPerspective(fovy, aspect, zNear, zFar);
}

void hook_gluLookAt(double eyeX, double eyeY, double eyeZ, double centerX, double centerY, double centerZ, double upX, double upY,
                     double upZ)
{
    return gluLookAt(eyeX, eyeY, eyeZ, centerX, centerY, centerZ, upX, upY, upZ);
}
