#define GL_GLEXT_PROTOTYPES
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "../config/config.h"
#include "../log/log.h"

#include "../graphics/fpsLimiter.h"
#include "../graphics/internalRes.h"
#include "../graphics/gui.h"

GLuint msGetSinkFBO(void);
void msOnSwap(void);
void irOnSwap(GLuint);

extern HWND g_Window;
extern char current_text[256];
extern double localFps;
bool winHidden = true;

static void *g_displayFunc = NULL;
static void *reshapeFunc = NULL;
static void *g_visibilityFunc = NULL;
static void *g_idleFunc = NULL;
static bool g_redisplayNeeded = true;

extern int drawableW;
extern int drawableH;

static HDC fontHdc = NULL;
static HFONT font = NULL;

void hook_GlutInit(int *argcp, char **argv) {}

void hook_GlutMainLoop(void)
{

    bool quit = false;
    while (!quit)
    {
        processEvents();
        int w, h;
        RECT clientRect;
        GetClientRect(g_Window, &clientRect);
        w = clientRect.right - clientRect.left;
        h = clientRect.bottom - clientRect.top;
        if (g_displayFunc && g_redisplayNeeded)
        {
            (((void *(*)(void))g_displayFunc)());
            g_redisplayNeeded = false;
        }
        if (reshapeFunc)
            (((void *(*)(int, int))reshapeFunc)(w, h));
        if (g_visibilityFunc)
            (((void *(*)(int))g_visibilityFunc)(1));
        if (g_idleFunc)
            (((void *(*)(void))g_idleFunc)());
        Sleep(1);
    }
    DestroyWindow(g_Window);
}


void hook_GlutSwapBuffers(void)
{
    irOnSwap(msGetSinkFBO()); /* guarantee the sink/composite FBO is scaled */
    msOnSwap();               /* resolve the sink FBO if the engine presented it dirty */
    HDC hdc = GetDC(g_Window);
    fpsLimiter();
    SwapBuffers(hdc);
    ReleaseDC(g_Window, hdc);
}

int hook_GlutEnterGameMode(void)
{
    if (!g_Window)
        startGraphics();
    return 0;
}

int hook_GlutGet(GLenum type)
{
    if (type == 0x66)
        return Config.display.width;
    if (type == 0x67)
        return Config.display.height;

    return 0;
}

void hook_GlutSetCursor(int glutCursor) {}
void hook_GlutInitWindowSize(int width, int height){}
void hook_GlutInitWindowPosition(int x, int y){}
void hook_GlutDisplayFunc(void (*callback)(void))
{
    g_displayFunc = callback;
}

void hook_GlutIdleFunc(void (*callback)(void))
{
    g_idleFunc = callback;
}

void hook_GlutInitDisplayMode(unsigned int mode){}
void hook_GlutGameModeString(const char *string){}

void hook_GlutPostRedisplay(void)
{
    g_redisplayNeeded = true;
}

void hook_GlutKeyboardFunc(void (*callback)(unsigned char, int, int)) {}
void hook_GlutLeaveGameMode() {}
int hook_GlutBitmapWidth(void *fontID, int character)
{
    return 9;
}

void hook_GlutBitmapCharacter(void *fontID, int character)
{
    static int penX = 0;
    GLint glColor[4];
    glGetIntegerv(GL_CURRENT_COLOR, glColor);

    COLORREF color = RGB((int)(glColor[0] * 255), (int)(glColor[1] * 255), (int)(glColor[2] * 255));

    GLint glPos[4];
    glGetIntegerv(GL_CURRENT_RASTER_POSITION, glPos);

    if (penX != glPos[0])
        penX = glPos[0];

    if (!fontHdc)
    {
        fontHdc = CreateCompatibleDC(NULL);
        font = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Courier New");
        SelectObject(fontHdc, font);
    }

    SIZE sz;
    GetTextExtentPoint32(fontHdc, (LPCSTR)&character, 1, &sz);

    int width = sz.cx;
    int height = sz.cy;
    int pitch = (width + 7) / 8;
    uint8_t *bitmap = (uint8_t *)malloc(pitch * height);
    if (!bitmap)
    {
        log_error(plfGUI, "Failed to allocate memory for bitmap\n");
        return;
    }
    memset(bitmap, 0, pitch * height);

    HBITMAP hbm = CreateCompatibleBitmap(fontHdc, width, height);
    HBITMAP oldBmp = (HBITMAP)SelectObject(fontHdc, hbm);
    
    RECT rc = {0, 0, width, height};
    SetTextColor(fontHdc, color);
    SetBkColor(fontHdc, RGB(0, 0, 0));
    DrawText(fontHdc, (LPCSTR)&character, 1, &rc, DT_LEFT | DT_TOP);

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    unsigned char *pixels = (unsigned char *)malloc(width * height * 4);
    GetDIBits(fontHdc, hbm, 0, height, pixels, &bmi, DIB_RGB_COLORS);

    for (int y = 0; y < height; y++)
    {
        uint8_t byte = 0;
        int bit = 0;
        for (int x = 0; x < width; x++)
        {
            unsigned char *pixel = pixels + (y * width + x) * 4;
            if (pixel[0] > 128 || pixel[1] > 128 || pixel[2] > 128)
                byte |= (1 << (7 - bit));

            bit++;
            if (bit == 8 || x == width - 1)
            {
                bitmap[y * pitch + x / 8] = byte;
                byte = 0;
                bit = 0;
            }
        }
    }

    free(pixels);
    SelectObject(fontHdc, oldBmp);
    DeleteObject(hbm);

    GLint swbytes, lsbfirst, rowlen, skiprows, skippix, align;

    glGetIntegerv(GL_UNPACK_SWAP_BYTES, &swbytes);
    glGetIntegerv(GL_UNPACK_LSB_FIRST, &lsbfirst);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowlen);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skiprows);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skippix);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);

    glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_FALSE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glBitmap(width, height, 0, 0, (float)(width), 0.0, bitmap);

    glPixelStorei(GL_UNPACK_SWAP_BYTES, swbytes);
    glPixelStorei(GL_UNPACK_LSB_FIRST, lsbfirst);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, rowlen);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, skiprows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, skippix);
    glPixelStorei(GL_UNPACK_ALIGNMENT, align);

    free(bitmap);
}