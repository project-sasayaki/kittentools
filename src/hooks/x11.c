#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>

#include "x11.h"
#include "../graphics/gui.h"
#include "../log/log.h"
#include "../config/config.h"



int hook_XInitThreads()
{
    return 1;
}

static char s_dummyDisplay[4096] = {0};
static char s_commandBuffer[8192] = {0};
static char s_dummyScreens[4096] = {0};

// The single Display handle returned to the game; also reused by
// hook_GlxGetCurrentDisplay so both calls yield the same pointer.
Display *g_Display = NULL;

Display *hook_XOpenDisplay(const char *name)
{
    log_debug(plfGUI, "XOpenDisplay(\"%s\")\n", name ? name : "NULL");
    *(int *)(s_dummyDisplay + 0x84) = 0;                                           // Offset 0x84: Default Screen (int)
    *(void **)(s_dummyDisplay + 0x8C) = s_dummyScreens;                            // Offset 0x8C: Screens pointer
    *(Window *)(s_dummyScreens + 0x08) = 0;                                        // Offset 0x08 : window ID
    *(int *)(s_dummyScreens + 0x0C) = Config.display.width;                          // Offset 0x0C: Width
    *(int *)(s_dummyScreens + 0x10) = Config.display.height;                         // Offset 0x10: Height
    *(char **)(s_dummyDisplay + 0x6C) = s_commandBuffer;                           // Offset 0x6C: Current buffer pointer??
    *(char **)(s_dummyDisplay + 0x70) = s_commandBuffer + sizeof(s_commandBuffer); // Offset 0x70: bufmax??
    g_Display = (Display *)&s_dummyDisplay;
    return g_Display;
}

int hook_XCloseDisplay(Display *dpy)
{
    return 0;
}

Window hook_XCreateWindow(Display *dpy, Window parent, int x, int y, unsigned int width, unsigned int height,
                           unsigned int border_width, int depth, unsigned int class_type, void *visual, unsigned long valuemask,
                           XSetWindowAttributes *attributes)
{
    log_debug(plfGUI, "XCreateWindow: %ux%u\n", Config.display.width, Config.display.height);

    if (!g_Window)
    {
        startGraphics();
    }

    return (Window)g_Window;
}

int hook_XDestroyWindow(Display *dpy, Window win)
{
    return 1;
}

void hook_XMapWindow(Display *dpy, Window win)
{
    return;
}

int hook_XMoveWindow(Display *dpy, Window win, int x, int y)
{
    return 1;
}

int hook_XSync(Display *dpy, int discard)
{
    return 1;
}
int hook_XFlush(Display *dpy)
{
    return 1;
}

void hook_XLockDisplay(Display *dpy)
{
}
void hook_XUnlockDisplay(Display *dpy)
{
}
void hook_XSetErrorHandler(void *handler)
{
}
int hook_XGetErrorText(Display *dpy, int code, char *buffer, int length)
{
    if (buffer && length > 0)
        buffer[0] = '\0';
    return 0;
}

Atom hook_XInternAtom(Display *dpy, const char *atom_name, bool create_if_not_exist)
{
    return 1;
}

int hook_XPending(Display *dpy)
{
    return 0;
}

void hook_XNextEvent(Display *dpy, XEvent *event_return)
{
    return;
}

int hook_XCheckTypedEvent(Display *dpy, int type, XEvent *event_return)
{
    return 0;
}

int hook_XSelectInput(Display *dpy, Window win, long event_mask)
{
    return 0;
}
int hook_XSetCloseDownMode(Display *dpy, int close_mode)
{
    return 1;
}

int hook_XSetInputFocus(Display *dpy, Window win, int revert_to, Time time)
{
    return 1;
}

int hook_XTranslateCoordinates(Display *dpy, Window src_w, Window dest_w, int src_x, int src_y, int *dest_x_return, int *dest_y_return,
                                Window *child_return)
{
    HWND srcWin = g_Window;
    HWND destWin = g_Window;

    int srcWinX = 0;
    int srcWinY = 0;
    int destWinX = 0;
    int destWinY = 0;

    if (srcWin)
    {
        RECT rect;
        GetWindowRect(srcWin, &rect);
        srcWinX = rect.left;
        srcWinY = rect.top;
    }
    if (destWin)
    {
        RECT rect;
        GetWindowRect(destWin, &rect);
        destWinX = rect.left;
        destWinY = rect.top;
    }

    int globalX = src_x + srcWinX;
    int globalY = src_y + srcWinY;

    if (dest_x_return)
        *dest_x_return = globalX - destWinX;
    if (dest_y_return)
        *dest_y_return = globalY - destWinY;
    if (child_return)
        *child_return = 0;
    return 1;
}

int hook_XQueryPointer(Display *dpy, Window win, Window *root_return, Window *child_return, int *root_x_return, int *root_y_return,
                        int *win_x_return, int *win_y_return, unsigned int *mask_return)
{
    return 0;
}

void hook_XWarpPointer(Display *dpy, Window src_w, Window dest_w, int src_x, int src_y, unsigned int src_width, unsigned int src_height,
                        int dest_x, int dest_y)
{
}

void hook_XGrabPointer(Display *dpy, Window win, int owner_events, unsigned int event_mask, int pointer_mode, int keyboard_mode,
                        Window confine_to, Cursor cursor, Time time)
{
}

void hook_XUngrabPointer(Display *dpy, Time time)
{
}

int hook_XGrabKeyboard(Display *dpy, Window win, int owner_events, int pointer_mode, int keyboard_mode, Time time)
{
    return 0;
}

int hook_XUngrabKeyboard(Display *dpy, Time time)
{
    return 0;
}

int hook_XAutoRepeatOn(Display *dpy)
{
    return 1;
}

int hook_XParseColor(Display *dpy, Colormap colormap, const char *spec, int *color_return)
{
    return 1;
}

int hook_XLookupString(XKeyEvent *key_event, char *buffer_return, int buffer_size, int *keysym_return, int *status_return)
{
    return 0;
}

int hook_XStoreName(Display *dpy, Window win, const char *name)
{
    if (g_Window && name)
        SetWindowText(g_Window, name);
    return 0;
}

int hook_XSetWMProtocols(Display *dpy, Window win, Atom *protocols, int count)
{
    return 1;
}

int hook_XSetWMProperties(Display *dpy, Window win, XTextProperty *window_name, XTextProperty *icon_name, char **argv, int argc,
                           XSizeHints *normal_hints, XWMHints *wm_hints, XClassHint *class_hints)
{
    return 1;
}

int hook_XStringListToTextProperty(char **list, int count, XTextProperty *text_prop_return)
{
    if (text_prop_return && count > 0 && list && list[0])
    {
        size_t len = strlen(list[0]);
        text_prop_return->value = (unsigned char *)malloc(len + 1);
        if (text_prop_return->value)
            strcpy((char *)text_prop_return->value, list[0]);
        text_prop_return->encoding = 31;
        text_prop_return->format = 8;
        text_prop_return->nitems = len;
        return 1;
    }
    return 0;
}

void hook_XFree(void *data)
{
    if (data)
        free(data);
}

Pixmap hook_XCreatePixmapFromBitmapData(Display *dpy, Drawable drawable, char *data, unsigned int width, unsigned int height,
                                         unsigned long fg, unsigned long bg, unsigned int depth)
{
    return 2001;
}

Cursor hook_XCreatePixmapCursor(Display *dpy, Pixmap source, Pixmap mask, void *foreground_color, void *background_color, unsigned int x,
                                 unsigned int y)
{
    return 1001;
}

int hook_XFreePixmap(Display *dpy, Pixmap pixmap)
{
    return 1;
}

int hook_XFreeCursor(Display *dpy, Cursor cursor)
{
    return 1;
}
int hook_XDefineCursor(Display *dpy, Window w, Cursor c)
{
    return 0;
}

Cursor hook_XCreateFontCursor(Display *dpy, int shape)
{
    return 1001;
}

int hook_XF86VidModeGetAllModeLines(Display *dpy, int screen, int *modecount_return, XF86VidModeModeInfo ***modesinfo_return)
{
    if (modecount_return)
        *modecount_return = 1;

    if (modesinfo_return)
    {
        XF86VidModeModeInfo **modes = (XF86VidModeModeInfo **)malloc(sizeof(XF86VidModeModeInfo *));

        modes[0] = (XF86VidModeModeInfo *)malloc(sizeof(XF86VidModeModeInfo));
        memset(modes[0], 0, sizeof(XF86VidModeModeInfo));

        modes[0]->hdisplay = (unsigned short)Config.display.width;
        modes[0]->vdisplay = (unsigned short)Config.display.height;
        modes[0]->dotclock = 60000;
        modes[0]->htotal = modes[0]->hdisplay + 20;
        modes[0]->vtotal = modes[0]->vdisplay + 10;

        *modesinfo_return = modes;
    }
    return 1;
}

int hook_XF86VidModeGetModeLine(Display *dpy, int screen, int *dotclock_return, void *modeline)
{
    if (dotclock_return)
        *dotclock_return = 0;
    return 1;
}

int hook_XF86VidModeSwitchToMode(Display *dpy, int screen, void *modeline)
{
    return 1;
}

int hook_XF86VidModeSetViewPort(Display *dpy, int screen, int x, int y)
{
    return 1;
}

int hook_XF86VidModeGetViewPort(Display *dpy, int screen, int *x_return, int *y_return)
{
    if (x_return)
        *x_return = 0;
    if (y_return)
        *y_return = 0;
    return 1;
}

Atom hook_XCreateColormap(Display *dpy, Window w, Visual *visual, int alloc)
{
    return 1;
}