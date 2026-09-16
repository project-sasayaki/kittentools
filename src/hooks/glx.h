#pragma once
#include "x11types.h"

#include <stdbool.h>

// --- GLX FBConfig attribute tokens ---
#define GLX_USE_GL            1
#define GLX_BUFFER_SIZE       2
#define GLX_LEVEL             3
#define GLX_RGBA              4
#define GLX_DOUBLEBUFFER      5
#define GLX_STEREO            6
#define GLX_AUX_BUFFERS       7
#define GLX_RED_SIZE          8
#define GLX_GREEN_SIZE        9
#define GLX_BLUE_SIZE         10
#define GLX_ALPHA_SIZE        11
#define GLX_DEPTH_SIZE        12
#define GLX_STENCIL_SIZE      13
#define GLX_ACCUM_RED_SIZE    14
#define GLX_ACCUM_GREEN_SIZE  15
#define GLX_ACCUM_BLUE_SIZE   16
#define GLX_ACCUM_ALPHA_SIZE  17
#define GLX_VISUAL_ID         0x800B
#define GLX_DRAWABLE_TYPE     0x8010
#define GLX_RENDER_TYPE       0x8011
#define GLX_X_RENDERABLE      0x8012
#define GLX_FBCONFIG_ID       0x8013
#define GLX_RGBA_BIT          0x00000001
#define GLX_WINDOW_BIT        0x00000001
#define GLX_PBUFFER_BIT       0x00000004

// --- WGL ARB_pixel_format attribute tokens (FBConfig -> real pixel format) ---
#define WGL_DRAW_TO_WINDOW_ARB   0x2001
#define WGL_DRAW_TO_BITMAP_ARB   0x2002
#define WGL_SUPPORT_GL_ARB       0x2010
#define WGL_DOUBLE_BUFFER_ARB    0x2011
#define WGL_STEREO_ARB           0x2012
#define WGL_PIXEL_TYPE_ARB       0x2013
#define WGL_TYPE_RGBA_ARB        0x202B
#define WGL_TYPE_COLORINDEX_ARB  0x202C
#define WGL_COLOR_BITS_ARB       0x2014
#define WGL_RED_BITS_ARB         0x2015
#define WGL_GREEN_BITS_ARB       0x2016
#define WGL_BLUE_BITS_ARB        0x2017
#define WGL_ALPHA_BITS_ARB       0x2018
#define WGL_ACCUM_BITS_ARB       0x2019
#define WGL_ACCUM_RED_BITS_ARB   0x201A
#define WGL_ACCUM_GREEN_BITS_ARB 0x201B
#define WGL_ACCUM_BLUE_BITS_ARB  0x201C
#define WGL_ACCUM_ALPHA_BITS_ARB 0x201D
#define WGL_DEPTH_BITS_ARB       0x2022
#define WGL_STENCIL_BITS_ARB     0x2023
#define WGL_AUX_BUFFERS_ARB      0x2024
#define WGL_SAMPLE_BUFFERS_ARB   0x2041
#define WGL_SAMPLES_ARB          0x2042

typedef BOOL (WINAPI *PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC hdc, const int *piAttribIList,
                                                      const FLOAT *pfAttribFList, UINT nMaxFormats,
                                                      int *piFormats, UINT *nNumFormats);

// --------------------------------------------------------------------------
// FBConfig internal record (real GLX attributes -> Windows pixel format)
// --------------------------------------------------------------------------
struct __GLXFBConfigRec
{
    int doublebuffer;
    int stereo;
    int red_size, green_size, blue_size, alpha_size;
    int depth_size;
    int stencil_size;
    int accum_red, accum_green, accum_blue, accum_alpha;
    int aux_buffers;
    int drawable_type;
    int render_type;
    int x_renderable;
    int visual_id;
    int buffer_size;
    int pixel_format; // resolved WGL pixel format index (0 = unresolved)
};

extern struct __GLXFBConfigRec g_DesiredFBConfig;

// --------------------------------------------------------------------------
// GLX Types & Constants
// --------------------------------------------------------------------------
typedef XID GLXContextID;
typedef XID GLXPixmap;
typedef XID GLXDrawable;
typedef XID GLXPbuffer;
typedef XID GLXWindow;
typedef XID GLXFBConfigID;

typedef struct __GLXcontextRec *GLXContext;
typedef struct __GLXFBConfigRec *GLXFBConfig;

// Visual Info Structure (Compatible with X11)
typedef struct
{
    void *visual;
    VisualID visualid;
    int screen;
    int depth;
    int class_type;
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
    int colormap_size;
    int bits_per_rgb;
} XVisualInfo;


XVisualInfo *hook_GlxChooseVisual(Display *dpy, int screen, int *attribList);
GLXContext hook_GlxCreateContext(Display *dpy, XVisualInfo *vis, GLXContext shareList, bool direct);
void hook_GlxDestroyContext(Display *dpy, GLXContext ctx);
void hook_GlxSwapBuffers(Display *dpy, GLXDrawable drawable);
void *hook_GlxGetProcAddress(const char *procName);
bool hook_GlxQueryVersion(Display *dpy, int *major, int *minor);
const char *hook_GlxQueryExtensionsString(Display *dpy, int screen);
const char *hook_GlxQueryServerString(Display *dpy, int screen, int name);
const char *hook_GlxGetClientString(Display *dpy, int name);
Display *hook_GlxGetCurrentDisplay();
GLXContext hook_GlxGetCurrentContext();
GLXDrawable hook_GlxGetCurrentDrawable();

bool hook_GlxMakeCurrent(Display *dpy, GLXDrawable drawable, GLXContext ctx);
bool hook_GlxMakeContextCurrent(Display *dpy, GLXDrawable drawable, GLXDrawable read, GLXContext ctx);
int hook_GlxIsDirect(Display *dpy, GLXContext ctx);
int hook_GlxGetConfig(Display *dpy, XVisualInfo *vis, int attrib, int *value);
GLXFBConfig *hook_GlxChooseFBConfig(Display *dpy, int screen, int *attrib_list, int *nelements);
GLXFBConfig *hook_GlxChooseFBConfigSGIX(Display *dpy, int screen, int *attrib_list, int *nelements);
int hook_GlxGetFBConfigAttribSGIX(Display *dpy, GLXFBConfig config, int attribute, int *value);
XVisualInfo *hook_GlxGetVisualFromFBConfig(Display *dpy, GLXFBConfig config);
GLXContext hook_GlxCreateContextWithConfigSGIX(Display *dpy, GLXFBConfig config, int render_type, GLXContext share_list, bool direct);
int hook_GlxSwapIntervalSGI(int interval);
GLXPbuffer hook_GlxCreatePbuffer(Display *dpy, GLXFBConfig config, const int *attrib_list);
void hook_GlxDestroyPbuffer(Display *dpy, GLXPbuffer pbuf);
GLXPbuffer hook_GlxCreateGLXPbufferSGIX(Display *dpy, GLXFBConfig config, int width, int height, int *attrib_list);
void hook_GlxDestroyGLXPbufferSGIX(Display *dpy, GLXPbuffer pbuf);
GLXContext hook_GlxCreateNewContext(Display *dpy, GLXFBConfig config, int render_type, GLXContext share_list, bool direct);
void hook_gluPerspective(double fovy, double aspect, double zNear, double zFar);
void hook_gluLookAt(double eyeX, double eyeY, double eyeZ, double centerX, double centerY, double centerZ, double upX, double upY,double upZ);