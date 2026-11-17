#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* glad must come before <GL/gl.h>. */
#include "../../graphics/internalRes.h"
#include "../../graphics/msaa.h"
#include "../../graphics/textureFilter.h"
#include "wmmt3.h"
#include "wmmt3Graphics.h"
#include "../patchlib.h"
#include "../../config/config.h"
#include "../../elfLoader/elfLoader.h"
#include "../../elfLoader/glHooks.h"
#include "../../elfLoader/symbol.h"
#include "../../graphics/fpsLimiter.h"
#include "../../graphics/gui.h"
#include "../../hooks/glx.h"
#include "../../log/log.h"

static const char *g_windowTitle;

#pragma pack(push, 1)
typedef struct
{
    char ident[4];
    uint32_t unknown[5];
    uint32_t width;
    uint32_t height;
    uint32_t refreshMilliHz;
} AdmMode;

typedef struct
{
    char ident[4];
    HWND window;
} AdmWindow;
#pragma pack(pop)

/* The game takes its size from config.lua's SCREEN_XSIZE/YSIZE; a larger size
 * renders into a stand-in framebuffer scaled into the window. */
#define MAX_SCALE_STEPS 3

#define CABINET_WIDTH 640
#define CABINET_HEIGHT 480

static int g_gameWidth = CABINET_WIDTH, g_gameHeight = CABINET_HEIGHT;
static int g_baseWidth = CABINET_WIDTH, g_baseHeight = CABINET_HEIGHT;
static int g_outputWidth = CABINET_WIDTH, g_outputHeight = CABINET_HEIGHT;
static int g_samples;
static bool g_stretch;
static GLuint g_sceneFbo, g_sceneColor, g_sceneDepth;
static GLuint g_resolveFbo, g_resolveColor;
static GLuint g_stepFbo[MAX_SCALE_STEPS], g_stepColor[MAX_SCALE_STEPS];
static int g_steps;

static bool native_size(void)
{
    return g_gameWidth == CABINET_WIDTH && g_gameHeight == CABINET_HEIGHT;
}

#define MAX_SIDE 8192

void wmmt3_configure_resolution(void)
{
    bool cabinetSize = Config.wmmt3.cabinet_resolution;
    int outputWidth, outputHeight;
    if (Config.wmmt3.fullscreen)
    {
        DEVMODE dm;
        ZeroMemory(&dm, sizeof(dm));
        dm.dmSize = sizeof(dm);
        EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm);
        outputWidth = (int)dm.dmPelsWidth;
        outputHeight = (int)dm.dmPelsHeight;
    }
    else if (cabinetSize)
    {
        outputWidth = CABINET_WIDTH;
        outputHeight = CABINET_HEIGHT;
    }
    else
    {
        outputWidth = Config.wmmt3.window_width;
        outputHeight = Config.wmmt3.window_height;
    }
    if (outputWidth < 320)
        outputWidth = 320;
    if (outputHeight < 240)
        outputHeight = 240;

    if (cabinetSize)
    {
        g_baseWidth = CABINET_WIDTH;
        g_baseHeight = CABINET_HEIGHT;
    }
    else
    {
        g_baseHeight = outputHeight & ~1;
        g_baseWidth = (g_baseHeight * 4 / 3) & ~1;
        if (g_baseWidth > outputWidth)
        {
            g_baseWidth = outputWidth & ~1;
            g_baseHeight = (g_baseWidth * 3 / 4) & ~1;
        }
    }

    int wanted = Config.wmmt3.render_scale;
    int scale = wanted >= 8 ? 8 : wanted >= 4 ? 4 : wanted >= 2 ? 2 : 1;
    while (scale > 1 && (g_baseWidth * scale > MAX_SIDE || g_baseHeight * scale > MAX_SIDE))
        scale /= 2;
    if (scale != wanted && wanted > 1)
        log_warn(plfWmmt3, "Resolution: render_scale %d is used as %dx", wanted, scale);
    g_steps = scale == 8 ? 3 : scale == 4 ? 2 : scale == 2 ? 1 : 0;
    g_gameWidth = g_baseWidth * scale;
    g_gameHeight = g_baseHeight * scale;
    g_samples = Config.wmmt3.msaa_samples > 0 ? Config.wmmt3.msaa_samples : 0;
    g_outputWidth = outputWidth;
    g_outputHeight = outputHeight;
    g_stretch = Config.wmmt3.stretch;

    /* The window and the texture filter are shared with other games and read [display];
     * scaling and multisampling are done here, so the shared ones stay off. */
    Config.display.fullscreen = Config.wmmt3.fullscreen;
    Config.display.borderless = Config.wmmt3.borderless;
    Config.display.width = outputWidth;
    Config.display.height = outputHeight;
    Config.display.render_scale = 1;
    Config.display.msaa_samples = 0;
    Config.display.anisotropic = Config.wmmt3.anisotropic;
    irInit();
    msInit();
    tfInit();
    log_info(plfWmmt3, "Resolution: the game renders at %dx%d, shown at %dx%d in %dx%d%s", g_gameWidth,
             g_gameHeight, g_baseWidth, g_baseHeight, outputWidth, outputHeight,
             g_baseWidth == outputWidth && g_baseHeight == outputHeight ? ""
             : g_stretch ? " (stretched)" : " (4:3, black bars)");
}

static GLuint colour_buffer(int width, int height, int samples)
{
    GLuint renderbuffer = 0;
    glGenRenderbuffers(1, &renderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    if (samples > 0)
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
    else
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    return renderbuffer;
}

static bool colour_target(int width, int height, GLuint *fbo, GLuint *colour)
{
    *colour = colour_buffer(width, height, 0);
    glGenFramebuffers(1, fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, *colour);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

static void blit(GLuint from, int fromWidth, int fromHeight, GLuint to, int x, int y, int width, int height)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, from);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, to);
    glBlitFramebuffer(0, 0, fromWidth, fromHeight, x, y, x + width, y + height, GL_COLOR_BUFFER_BIT,
                      width == fromWidth && height == fromHeight ? GL_NEAREST : GL_LINEAR);
}

static GLuint resolve_scene(void)
{
    GLint draw = 0, read = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    blit(g_sceneFbo, g_gameWidth, g_gameHeight, g_resolveFbo, 0, 0, g_gameWidth, g_gameHeight);
    if (scissor)
        glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
    return g_resolveFbo;
}

static void create_scene_target(void)
{
    if (g_gameWidth == g_outputWidth && g_gameHeight == g_outputHeight && !g_stretch && g_samples <= 0)
        return;

    if (g_samples > 0)
    {
        GLint maxSamples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        if (g_samples > maxSamples)
        {
            log_warn(plfWmmt3, "Resolution: msaa_samples %d is more than this GPU's %d", g_samples, maxSamples);
            g_samples = maxSamples;
        }
    }

    glGenFramebuffers(1, &g_sceneFbo);
    g_sceneColor = colour_buffer(g_gameWidth, g_gameHeight, g_samples);
    glGenRenderbuffers(1, &g_sceneDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, g_sceneDepth);
    if (g_samples > 0)
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, g_samples, GL_DEPTH24_STENCIL8, g_gameWidth,
                                         g_gameHeight);
    else
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, g_gameWidth, g_gameHeight);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, g_sceneFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_sceneColor);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g_sceneDepth);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        log_error(plfWmmt3, "Resolution: the %dx%d render target is incomplete (0x%04x); drawing straight "
                  "into the window", g_gameWidth, g_gameHeight, status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &g_sceneFbo);
        glDeleteRenderbuffers(1, &g_sceneColor);
        glDeleteRenderbuffers(1, &g_sceneDepth);
        g_sceneFbo = 0;
        return;
    }
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    bool ok = true;
    if (g_samples > 0)
        ok = colour_target(g_gameWidth, g_gameHeight, &g_resolveFbo, &g_resolveColor);
    for (int i = 0; ok && i < g_steps; i++)
        ok = colour_target(g_gameWidth >> (i + 1), g_gameHeight >> (i + 1), &g_stepFbo[i], &g_stepColor[i]);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    if (!ok)
    {
        log_error(plfWmmt3, "Resolution: a resolve or scale-down target is incomplete; MSAA and render_scale "
                  "are off");
        g_steps = 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_sceneFbo);
    glViewport(0, 0, g_gameWidth, g_gameHeight);
    GLHooks_SetDefaultFramebuffer(g_sceneFbo);
    if (g_samples > 0 && g_resolveFbo && ok)
        GLHooks_SetDefaultReadResolver(resolve_scene);
    log_info(plfWmmt3, "Resolution: the game draws into a %dx%d render target (%d samples, %d scale-down steps)",
             g_gameWidth, g_gameHeight, g_samples, g_steps);
}

static GLint g_savedDraw, g_savedRead;
static GLboolean g_savedScissor, g_savedMask[4];
static GLfloat g_savedClear[4];

static void present_scene(void)
{
    if (!g_sceneFbo)
        return;
    RECT client;
    GetClientRect(g_Window, &client);
    int width = client.right - client.left, height = client.bottom - client.top;

    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &g_savedDraw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &g_savedRead);
    g_savedScissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetBooleanv(GL_COLOR_WRITEMASK, g_savedMask);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, g_savedClear);

    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    GLuint source = g_sceneFbo;
    int sourceWidth = g_gameWidth, sourceHeight = g_gameHeight;
    if (g_samples > 0 && g_resolveFbo)
    {
        blit(g_sceneFbo, g_gameWidth, g_gameHeight, g_resolveFbo, 0, 0, g_gameWidth, g_gameHeight);
        source = g_resolveFbo;
    }
    for (int i = 0; i < g_steps; i++)
    {
        blit(source, sourceWidth, sourceHeight, g_stepFbo[i], 0, 0, sourceWidth / 2, sourceHeight / 2);
        source = g_stepFbo[i];
        sourceWidth /= 2;
        sourceHeight /= 2;
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (width > 0 && height > 0)
    {
        int x = 0, y = 0, w = width, h = height;
        if (!g_stretch)
        {
            if ((long long)width * sourceHeight > (long long)height * sourceWidth)
                w = (int)((long long)height * sourceWidth / sourceHeight);
            else
                h = (int)((long long)width * sourceHeight / sourceWidth);
            x = (width - w) / 2;
            y = (height - h) / 2;
        }
        blit(source, sourceWidth, sourceHeight, 0, x, y, w, h);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

static void restore_scene(void)
{
    if (!g_sceneFbo)
        return;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)g_savedDraw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)g_savedRead);
    if (g_savedScissor)
        glEnable(GL_SCISSOR_TEST);
    glColorMask(g_savedMask[0], g_savedMask[1], g_savedMask[2], g_savedMask[3]);
    glClearColor(g_savedClear[0], g_savedClear[1], g_savedClear[2], g_savedClear[3]);
}

typedef int (*LuaGetGlobalFn)(void *state, const char *name);
typedef void (*LuaSetTopFn)(void *state, int index);
typedef void (*LuaPushNumberFn)(void *state, double value);
static LuaGetGlobalFn next_lua_getglobal;
static LuaSetTopFn guest_lua_settop;
static LuaPushNumberFn guest_lua_pushnumber;

static int hook_lua_getglobal(void *state, const char *name)
{
    int result = next_lua_getglobal(state, name);
    if (name && (strcmp(name, "SCREEN_XSIZE") == 0 || strcmp(name, "SCREEN_YSIZE") == 0))
    {
        guest_lua_settop(state, -2);
        guest_lua_pushnumber(state, name[7] == 'X' ? g_gameWidth : g_gameHeight);
    }
    return result;
}

/* The minimap viewport is in raw 640x480 pixels. */
typedef void (*MiniMapDrawFn)(void *self);
typedef void (*SetViewportFn)(void *self, int x, int y, int width, int height, float nearPlane, float farPlane);
static MiniMapDrawFn next_minimap_draw;
static SetViewportFn next_setViewport;
static volatile bool g_inMiniMap;

static void hook_minimap_draw(void *self)
{
    g_inMiniMap = true;
    next_minimap_draw(self);
    g_inMiniMap = false;
}

static int scale_to(int value, int size, int native)
{
    return (int)lround((double)value * size / native);
}

static void hook_setViewport(void *self, int x, int y, int width, int height, float nearPlane, float farPlane)
{
    if (g_inMiniMap && width == 88 && height == 82)
    {
        x = scale_to(x, g_gameWidth, CABINET_WIDTH);
        y = scale_to(y, g_gameHeight, CABINET_HEIGHT);
        width = scale_to(width, g_gameWidth, CABINET_WIDTH);
        height = scale_to(height, g_gameHeight, CABINET_HEIGHT);
    }
    next_setViewport(self, x, y, width, height, nearPlane, farPlane);
}

static DWORD g_renderThread;

static AdmMode g_mode;
static AdmMode *g_modeList[2];
static uint32_t g_fbConfig;
static AdmWindow g_admWindow;

#define adm_success stubRetOne

static const char *adm_get_string(void)
{
    return "KittenTools";
}

static AdmMode **adm_choose_mode(void)
{
    memset(&g_mode, 0, sizeof(g_mode));
    memcpy(g_mode.ident, "MOCF", 4);
    g_mode.width = (uint32_t)g_gameWidth;
    g_mode.height = (uint32_t)g_gameHeight;
    g_mode.refreshMilliHz = 60000;
    g_modeList[0] = &g_mode;
    g_modeList[1] = NULL;
    return g_modeList;
}

static uint32_t *adm_choose_fb_config(void)
{
    return &g_fbConfig;
}

static AdmWindow *adm_create_window(void)
{
    startGraphics();
    g_renderThread = GetCurrentThreadId();

    size_t patched = elf_patch_writable_code_stubs("gl", GLHooks_GetProcAddress);
    log_debug(plfWmmt3, "Pointed %u OpenGL entry points at the host driver", (unsigned)patched);
    if (g_Window)
        create_scene_target();

    memcpy(g_admWindow.ident, "WNDW", 4);
    if (g_Window && g_windowTitle)
        SetWindowTextA(g_Window, g_windowTitle);
    g_admWindow.window = g_Window;
    return g_Window ? &g_admWindow : NULL;
}

static int adm_make_context_current(void)
{
    if (wglGetCurrentContext() == g_GLContext)
        return 1;
    return makeGLCurrent(g_Window, g_GLContext) == 0 ? 1 : 0;
}

static int adm_get_device_attrib(int device, int attribute, int *value)
{
    (void)device;
    (void)attribute;
    if (value)
        *value = 0;
    return 1;
}

static int adm_swap_interval(int interval)
{
    hook_GlxSwapIntervalSGI(interval);
    return 1;
}

/* Loader-thread texture work runs on the render thread's context. */
typedef int (*CreateTextureHandleFn)(void *self, int width, int height);
typedef int (*SetTextureFn)(void *self, int texture, int image);
typedef int (*SetTextureRegionFn)(void *self, int texture, int x, int y, int width, int height, int format,
                                  void *image);

static CreateTextureHandleFn next_createTextureHandle;
static SetTextureFn next_setTexture;
static SetTextureRegionFn next_setTextureRegion;

typedef enum
{
    TEXTURE_CREATE,
    TEXTURE_SET,
    TEXTURE_SET_REGION
} TextureWorkKind;

typedef struct TextureWork
{
    struct TextureWork *next;
    TextureWorkKind kind;
    void *self;
    int args[7];
    void *image;
} TextureWork;

static CRITICAL_SECTION g_textureLock;
static TextureWork *g_textureHead, *g_textureTail;

static bool on_render_thread(void)
{
    return g_renderThread == 0 || GetCurrentThreadId() == g_renderThread;
}

static bool queue_texture_work(const TextureWork *work)
{
    TextureWork *copy = (TextureWork *)malloc(sizeof(*copy));
    if (!copy)
        return false;
    *copy = *work;
    copy->next = NULL;

    EnterCriticalSection(&g_textureLock);
    if (g_textureTail)
        g_textureTail->next = copy;
    else
        g_textureHead = copy;
    g_textureTail = copy;
    LeaveCriticalSection(&g_textureLock);

    static bool announced = false;
    if (!announced)
    {
        announced = true;
        log_debug(plfWmmt3, "Texture work arrived from a loader thread; running it on the render thread");
    }
    return true;
}

static void run_texture_work(void)
{
    EnterCriticalSection(&g_textureLock);
    TextureWork *work = g_textureHead;
    g_textureHead = g_textureTail = NULL;
    LeaveCriticalSection(&g_textureLock);

    while (work)
    {
        TextureWork *next = work->next;
        switch (work->kind)
        {
            case TEXTURE_CREATE:
                next_createTextureHandle(work->self, work->args[0], work->args[1]);
                break;
            case TEXTURE_SET:
                next_setTexture(work->self, work->args[0], work->args[1]);
                break;
            case TEXTURE_SET_REGION:
                next_setTextureRegion(work->self, work->args[0], work->args[1], work->args[2], work->args[3],
                                      work->args[4], work->args[5], work->image);
                break;
        }
        free(work);
        work = next;
    }
}

static int hook_createTextureHandle(void *self, int width, int height)
{
    if (on_render_thread())
        return next_createTextureHandle(self, width, height);
    TextureWork work = {.kind = TEXTURE_CREATE, .self = self, .args = {width, height}};
    return queue_texture_work(&work) ? 1 : next_createTextureHandle(self, width, height);
}

static int hook_setTexture(void *self, int texture, int image)
{
    if (on_render_thread())
        return next_setTexture(self, texture, image);
    TextureWork work = {.kind = TEXTURE_SET, .self = self, .args = {texture, image}};
    return queue_texture_work(&work) ? 1 : next_setTexture(self, texture, image);
}

static int hook_setTextureRegion(void *self, int texture, int x, int y, int width, int height, int format,
                                 void *image)
{
    if (on_render_thread())
        return next_setTextureRegion(self, texture, x, y, width, height, format, image);
    TextureWork work = {.kind = TEXTURE_SET_REGION, .self = self,
                        .args = {texture, x, y, width, height, format}, .image = image};
    return queue_texture_work(&work) ? 1 : next_setTextureRegion(self, texture, x, y, width, height, format, image);
}

/* KITTEN_DUMP_FRAME=<n> */
static void dump_frame_if_asked(unsigned frame, GLenum buffer, const char *tag)
{
    static int every = -1;
    if (every < 0)
    {
        const char *value = getenv("KITTEN_DUMP_FRAME");
        every = value ? atoi(value) : 0;
    }
    if (every <= 0 || frame == 0 || frame % (unsigned)every != 0)
        return;

    int width = g_outputWidth, height = g_outputHeight;
    size_t rowBytes = (size_t)width * 3;
    size_t padded = (rowBytes + 3) & ~(size_t)3;
    uint8_t *pixels = (uint8_t *)malloc(padded * (size_t)height);
    if (!pixels)
        return;
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadBuffer(buffer);
    glReadPixels(0, 0, width, height, GL_BGR, GL_UNSIGNED_BYTE, pixels);
    glReadBuffer(GL_BACK);

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "kitten/frame_%u%s.bmp", frame, tag);
    FILE *out = fopen(path, "wb");
    if (out)
    {
        BITMAPFILEHEADER fileHeader = {0};
        BITMAPINFOHEADER info = {0};
        fileHeader.bfType = 0x4D42;
        fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(info);
        fileHeader.bfSize = fileHeader.bfOffBits + (DWORD)(padded * height);
        info.biSize = sizeof(info);
        info.biWidth = width;
        info.biHeight = height; /* bottom-up, as GL returns it */
        info.biPlanes = 1;
        info.biBitCount = 24;
        fwrite(&fileHeader, sizeof(fileHeader), 1, out);
        fwrite(&info, sizeof(info), 1, out);
        fwrite(pixels, padded * height, 1, out);
        fclose(out);
        log_info(plfWmmt3, "Wrote %s", path);
    }
    free(pixels);
}

static int adm_swap_buffers(AdmWindow *window)
{
    (void)window;
    run_texture_work();
    static unsigned frames = 0;
    if (frames++ % 600 == 0)
        log_debug(plfWmmt3, "Presented frame %u", frames);
    processEvents();
    irOnSwap(msGetSinkFBO());
    msOnSwap();
    present_scene();
    dump_frame_if_asked(frames, GL_BACK, "");
    fpsLimiter();
    frameTimeLog_beforeSwap();
    SwapBuffers(g_WindowDC);
    frameTimeLog_afterSwap();
    dump_frame_if_asked(frames, GL_FRONT, "_front");
    restore_scene();
    return 1;
}

static void hook_with_next(const char *name, void *replacement, void **next)
{
    if (!wmmt3_hook(name, replacement, next))
        log_warn(plfWmmt3, "Could not hook %s; leaving it alone", name);
}

static void hook(const char *name, void *replacement)
{
    hook_with_next(name, replacement, NULL);
}

void wmmt3_set_window_title(const char *title)
{
    g_windowTitle = title;
}

void wmmt3_install_graphics_hooks(void)
{
    /* Alchemy feeds conventional arrays; the Initial D remaps would drop its texcoords and colours. */
    GLHooks_SetIdasArbCompat(0);

    hook("admvt_setup", adm_success);
    hook("admShutdown", adm_success);
    hook("admGetString", adm_get_string);
    hook("admGetNumDevices", adm_success);
    hook("admInitDevicei", adm_success);
    hook("admChooseModeConfigi", adm_choose_mode);
    hook("admModeConfigi", adm_success);
    hook("admChooseFBConfigi", adm_choose_fb_config);
    hook("admCreateScreeni", adm_success);
    hook("admCreateGraphicsContext", adm_success);
    hook("admCreateWindowi", adm_create_window);
    hook("admDisplayScreen", adm_success);
    hook("admMakeContextCurrent", adm_make_context_current);
    hook("admSwapInterval", adm_swap_interval);
    hook("admCursorAttribi", adm_success);
    hook("admGetDeviceAttribi", adm_get_device_attrib);
    hook("admSwapBuffers", adm_swap_buffers);
    hook("admSetMonitorGamma", adm_success);

    InitializeCriticalSection(&g_textureLock);
    hook_with_next("_ZN24clAlchemyTextureAccessor19createTextureHandleEii", hook_createTextureHandle,
                   (void **)&next_createTextureHandle);
    hook_with_next("_ZN3Gap3Gfx19igAGLEVisualContext10setTextureEii", hook_setTexture, (void **)&next_setTexture);
    hook_with_next("_ZN3Gap3Gfx19igAGLEVisualContext16setTextureRegionEiiiiiiPNS0_7igImageE",
                   hook_setTextureRegion, (void **)&next_setTextureRegion);

    if (!native_size())
    {
        guest_lua_settop = (LuaSetTopFn)get_native_symbol_ptr("lua_settop");
        guest_lua_pushnumber = (LuaPushNumberFn)get_native_symbol_ptr("lua_pushnumber");
        if (guest_lua_settop && guest_lua_pushnumber)
            hook_with_next("lua_getglobal", hook_lua_getglobal, (void **)&next_lua_getglobal);
        else
            log_warn(plfWmmt3, "Resolution: no lua_settop / lua_pushnumber; the game stays at 640x480");
        hook_with_next("_ZN9clMiniMap4drawEv", hook_minimap_draw, (void **)&next_minimap_draw);
        hook_with_next("_ZN3Gap3Gfx19igAGLEVisualContext11setViewportEiiiiff", hook_setViewport,
                       (void **)&next_setViewport);
    }

    initFpsLimiter(600, 10, true);
}
