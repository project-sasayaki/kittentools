#pragma once

#include <stdbool.h>
#include "x11types.h"


// --- Display & Window Management ---
Display *hook_XOpenDisplay(const char *name);
extern Display *g_Display;
int hook_XCloseDisplay(Display *dpy);
Window hook_XCreateWindow(Display *dpy, Window parent, int x, int y, unsigned int width, unsigned int height,
                            unsigned int border_width, int depth, unsigned int class_type, void *visual, unsigned long valuemask,
                            XSetWindowAttributes *attributes);
int hook_XDestroyWindow(Display *dpy, Window win);
void hook_XMapWindow(Display *dpy, Window win);
int hook_XMoveWindow(Display *dpy, Window win, int x, int y);
int hook_XSync(Display *dpy, int discard);
int hook_XFlush(Display *dpy);
void hook_XLockDisplay(Display *dpy);
void hook_XUnlockDisplay(Display *dpy);
void hook_XSetErrorHandler(void *handler);
int hook_XGetErrorText(Display *dpy, int code, char *buffer, int length);

Atom hook_XInternAtom(Display *dpy, const char *atom_name, bool create_if_not_exist);

Atom hook_XCreateColormap(Display *dpy, Window w, Visual *visual, int alloc);

// --- Event Handling ---
int hook_XPending(Display *dpy);
void hook_XNextEvent(Display *dpy, XEvent *event_return);
int hook_XCheckTypedEvent(Display *dpy, int type, XEvent *event_return);
int hook_XSelectInput(Display *dpy, Window win, long event_mask);
int hook_XSetCloseDownMode(Display *dpy, int close_mode);
int hook_XSetInputFocus(Display *dpy, Window win, int revert_to, Time time);

// --- Input (Mouse/Keyboard) ---
int hook_XQueryPointer(Display *dpy, Window win, Window *root_return, Window *child_return, int *root_x_return, int *root_y_return,
                            int *win_x_return, int *win_y_return, unsigned int *mask_return);
void hook_XWarpPointer(Display *dpy, Window src_w, Window dest_w, int src_x, int src_y, unsigned int src_width,
                            unsigned int src_height, int dest_x, int dest_y);
void hook_XGrabPointer(Display *dpy, Window win, int owner_events, unsigned int event_mask, int pointer_mode, int keyboard_mode,
                            Window confine_to, Cursor cursor, Time time);
void hook_XUngrabPointer(Display *dpy, Time time);
int hook_XGrabKeyboard(Display *dpy, Window win, int owner_events, int pointer_mode, int keyboard_mode, Time time);
int hook_XUngrabKeyboard(Display *dpy, Time time);
int hook_XAutoRepeatOn(Display *dpy);

// --- Attributes, Properties & WM Hints ---
Atom hook_XInternAtom(Display *dpy, const char *atom_name, bool create_if_not_exist);
int hook_XStoreName(Display *dpy, Window win, const char *name);
int hook_XParseColor(Display *dpy, Colormap colormap, const char *spec, int *color_return);

int hook_XLookupString(XKeyEvent *key_event, char *buffer_return, int buffer_size, int *keysym_return, int *status_return);
int hook_XTranslateCoordinates(Display *dpy, Window src_w, Window dest_w, int src_x, int src_y, int *dest_x_return,
                                    int *dest_y_return, Window *child_return);
// Missing members added:
int hook_XSetWMProtocols(Display *dpy, Window win, Atom *protocols, int count);
int hook_XSetWMProperties(Display *dpy, Window win, XTextProperty *window_name, XTextProperty *icon_name, char **argv, int argc,
                            XSizeHints *normal_hints, XWMHints *wm_hints, XClassHint *class_hints);
int hook_XStringListToTextProperty(char **list, int count, XTextProperty *text_prop_return);

// --- Pixmaps & Cursors ---
Pixmap hook_XCreatePixmapFromBitmapData(Display *dpy, Drawable drawable, char *data, unsigned int width, unsigned int height,
                                            unsigned long fg, unsigned long bg, unsigned int depth);
Cursor hook_XCreatePixmapCursor(Display *dpy, Pixmap source, Pixmap mask, void *foreground_color, void *background_color,
                                    unsigned int x, unsigned int y);
int hook_XFreePixmap(Display *dpy, Pixmap pixmap);
int hook_XFreeCursor(Display *dpy, Cursor cursor);
int hook_XDefineCursor(Display *dpy, Window w, Cursor c);
Cursor hook_XCreateFontCursor(Display *dpy, int shape);

// --- XF86VidMode Extension (Stubs) ---
int hook_XF86VidModeGetAllModeLines(Display *dpy, int screen, int *modecount_return, XF86VidModeModeInfo ***modesinfo_return);
int hook_XF86VidModeGetModeLine(Display *dpy, int screen, int *dotclock_return, void *modeline);
int hook_XF86VidModeSwitchToMode(Display *dpy, int screen, void *modeline);
int hook_XF86VidModeSetViewPort(Display *dpy, int screen, int x, int y);
int hook_XF86VidModeGetViewPort(Display *dpy, int screen, int *x_return, int *y_return);

// --- Misc / Legacy Stubs ---
int hook_XInitThreads();
void hook_XFree(void *data);