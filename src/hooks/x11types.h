#pragma once
#include <stdint.h>


// --------------------------------------------------------------------------
// X11 Basic Types
// --------------------------------------------------------------------------
typedef unsigned long XID;
typedef unsigned long Window;
typedef unsigned long Drawable;
typedef unsigned long VisualID;
typedef unsigned long Atom;
typedef unsigned long Time;
typedef unsigned long Colormap;

typedef struct {
    void *ext_data;
    VisualID visualid;
    int class_type;
    unsigned long red_mask, green_mask, blue_mask;
    int bits_per_rgb;
    int map_entries;
} Visual;

// --------------------------------------------------------------------------
// X11 Structure Definitions
// --------------------------------------------------------------------------

typedef struct {
    void *ext_data;
    struct _XDisplay *display;
    unsigned long root;
    int width, height;
    int mwidth, mheight;
    int ndepths;
    void *depths;
    int root_depth;
    void *root_visual;
    void *default_gc;
    unsigned long cmap;
    unsigned long white_pixel, black_pixel;
    int max_maps, min_maps;
    int backing_store;
    int save_unders;
    long root_input_mask;
} Screen;

typedef struct _XDisplay {
    void *ext_data;
    struct _XFreeFuncs *free_funcs;
    int fd;
    int conn_checker;
    int proto_major_version;
    int proto_minor_version;
    char *vendor;
    long resource_base;
    long resource_mask;
    long resource_id;
    int resource_shift;
    long (*resource_alloc)(struct _XDisplay *);
    int byte_order;
    int bitmap_unit;
    int bitmap_pad;
    int bitmap_bit_order;
    int nformats;
    void *pixmap_format;
    int vnumber;
    int release;
    void *head, *tail;
    int qlen;
    unsigned long last_request_read;
    unsigned long request;
    char *last_req;
    char *buffer;
    char *bufptr;
    char *bufmax;
    unsigned max_request_size;
    struct _XrmHashBucketRec *db;
    int (*synchandler)(struct _XDisplay *);
    char *display_name;
    int default_screen;
    int nscreens;
    Screen *screens;
    unsigned long motion_buffer;
    unsigned long flags;
    int min_keycode;
    int max_keycode;
    void *keysyms;
    void *modifiermap;
    int keysyms_per_keycode;
    char *xdefaults;
    char *scratch_buffer;
    unsigned long scratch_length;
    int ext_number;
    void *ext_procs;
    int (*event_vec[128])(struct _XDisplay *, void *, void *);
    int (*wire_vec[128])(struct _XDisplay *, void *, void *);
    unsigned long lock_meaning; // lock
    void *lock;
    void *async_handlers;
    unsigned long bigreq_size;
    void *lock_funcs;
    void *idlist_alloc;
    void *key_bindings;
    unsigned long cursor_font;
    void *atoms;
    unsigned int mode_switch;
    unsigned int num_lock;
    void *context_db;
    int (**error_vec)(struct _XDisplay *, void *, void *);
    void *cms;
    void *im_filters;
    void *qfree;
    unsigned long next_event_serial_num;
    struct _XExten *flushes;
    struct _XConnectionInfo *im_fd_info;
    int im_fd_length;
    struct _XConnWatchInfo *conn_watchers;
    int watcher_count;
    void *filedes;
    int (*savedsynchandler)(struct _XDisplay *);
    unsigned long resource_max;
    int xcmisc_opcode;
    void *xkb_info;
    void *trans_conn;
    struct _X11XCBPrivate *xcb;
    unsigned int next_cookie;
} Display;

typedef unsigned long XID;
typedef unsigned long Window;
typedef unsigned long Drawable;
typedef unsigned long Atom;
typedef unsigned long Atom;
typedef unsigned long Time;
typedef unsigned long VisualID;
typedef unsigned long Pixmap;
typedef unsigned long Cursor;
typedef unsigned long Colormap;

// --------------------------------------------------------------------------
// X11 Event Constants
// --------------------------------------------------------------------------
#define KeyPress 2
#define KeyRelease 3
#define ButtonPress 4
#define ButtonRelease 5
#define MotionNotify 6
#define EnterNotify 7
#define LeaveNotify 8
#define FocusIn 9
#define FocusOut 10
#define Expose 12
#define MapNotify 19
#define ClientMessage 33

// Event Masks (Simplified)
#define NoEventMask 0L
#define KeyPressMask (1L << 0)
#define KeyReleaseMask (1L << 1)
#define ButtonPressMask (1L << 2)
#define ButtonReleaseMask (1L << 3)
#define EnterWindowMask (1L << 4)
#define LeaveWindowMask (1L << 5)
#define PointerMotionMask (1L << 6)
#define PointerMotionHintMask (1L << 7)
#define Button1MotionMask (1L << 8)
#define Button2MotionMask (1L << 9)
#define Button3MotionMask (1L << 10)
#define Button4MotionMask (1L << 11)
#define Button5MotionMask (1L << 12)
#define ButtonMotionMask (1L << 13)
#define KeymapStateMask (1L << 14)
#define ExposureMask (1L << 15)
#define VisibilityChangeMask (1L << 16)
#define StructureNotifyMask (1L << 17)
#define ResizeRedirectMask (1L << 18)
#define SubstructureNotifyMask (1L << 19)
#define SubstructureRedirectMask (1L << 20)
#define FocusChangeMask (1L << 21)
#define PropertyChangeMask (1L << 22)
#define ColormapChangeMask (1L << 23)
#define OwnerGrabButtonMask (1L << 24)

// --------------------------------------------------------------------------
// X11 Event Structures
// --------------------------------------------------------------------------

typedef struct
{
    int type;
    unsigned long serial;
    int send_event;
    Display *display;
    Window window;
} XAnyEvent;

typedef struct
{
    int type;
    unsigned long serial;
    int send_event;
    Display *display;
    Window window;
    Window root;
    Window subwindow;
    Time time;
    int x, y;
    int x_root, y_root;
    unsigned int state;
    unsigned int keycode;
    int same_screen;
} XKeyEvent;

typedef struct
{
    int type;
    unsigned long serial;
    int send_event;
    Display *display;
    Window window;
    Window root;
    Window subwindow;
    Time time;
    int x, y;
    int x_root, y_root;
    unsigned int state;
    unsigned int button;
    int same_screen;
} XButtonEvent;

typedef struct {
    unsigned int dotclock;
    unsigned short hdisplay;
    unsigned short hsyncstart;
    unsigned short hsyncend;
    unsigned short htotal;
    unsigned short hskew;
    unsigned short vdisplay;
    unsigned short vsyncstart;
    unsigned short vsyncend;
    unsigned short vtotal;
    unsigned int flags;
    int privsize;
    int *private_data;
} XF86VidModeModeInfo;

typedef struct
{
    int type;
    unsigned long serial;
    int send_event;
    Display *display;
    Window window;
    Window root;
    Window subwindow;
    Time time;
    int x, y;
    int x_root, y_root;
    unsigned int state;
    char is_hint;
    int same_screen;
} XMotionEvent;

typedef struct
{
    int type;
    unsigned long serial;
    int send_event;
    Display *display;
    Window window;
    Atom message_type;
    int format;
    union {
        char b[20];
        short s[10];
        long l[5];
    } data;
} XClientMessageEvent;

// The Main Union
typedef union _XEvent {
    int type;
    XAnyEvent xany;
    XKeyEvent xkey;
    XButtonEvent xbutton;
    XMotionEvent xmotion;
    XClientMessageEvent xclient;
    long pad[24];
} XEvent;

// --------------------------------------------------------------------------
// X11 Attributes, Hints, Properties
// --------------------------------------------------------------------------

typedef struct
{
    unsigned char *value;
    Atom encoding;
    int format;
    unsigned long nitems;
} XTextProperty;

typedef struct
{
    long flags;
    int x, y;
    int width, height;
    int min_width, min_height;
    int max_width, max_height;
    int width_inc, height_inc;
    struct
    {
        int x;
        int y;
    } min_aspect, max_aspect;
    int base_width, base_height;
    int win_gravity;
} XSizeHints;

typedef struct
{
    long flags;
    int input;
    int initial_state;
    Pixmap icon_pixmap;
    Window icon_window;
    int icon_x, icon_y;
    Pixmap icon_mask;
    XID window_group;
} XWMHints;

typedef struct
{
    char *res_name;
    char *res_class;
} XClassHint;

typedef struct
{
    unsigned long background_pixel;
    unsigned long event_mask;
} XSetWindowAttributes;

typedef struct
{
    int x, y;
    int width, height;
    int border_width;
    int depth;
    void *visual;
    Window root;
    int class_type;
    int bit_gravity;
    int win_gravity;
    int backing_store;
    unsigned long backing_planes;
    unsigned long backing_pixel;
    int save_under;
    unsigned long colormap;
    int map_installed;
    int map_state;
    long all_event_masks;
    long your_event_mask;
    long do_not_propagate_mask;
    int override_redirect;
    void *screen;
} XWindowAttributes;
