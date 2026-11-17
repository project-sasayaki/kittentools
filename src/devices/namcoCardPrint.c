#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "namcoCardPrint.h"
#include "../config/config.h"
#include "../log/log.h"

#define IMAGE_WIDTH 640
#define IMAGE_HEIGHT 1019
#define PX_PER_DOT (640.0 / 432.0)
#define DOTS(n) ((int)((n) * PX_PER_DOT + 0.5))

#define HALF_WIDTH_DOTS 12
#define CHAR_HEIGHT_DOTS 24
#define LINE_PITCH_DOTS 28
#define LEFT_DOTS 64
#define TOP_DOTS 81

#define INK_R 96
#define INK_G 96
#define INK_B 148

#define MAX_BLOCKS 8
#define MAX_BLOCK_BYTES 1024

static uint8_t g_blocks[MAX_BLOCKS][MAX_BLOCK_BYTES];
static size_t g_blockLength[MAX_BLOCKS];
static int g_blockCount;

typedef struct
{
    UINT32 version;
    void *debugCallback;
    BOOL suppressBackgroundThread;
    BOOL suppressExternalCodecs;
} GdiplusStartupInputC;

#define PIXEL_FORMAT_32BPP_ARGB 0x26200A

int WINAPI GdiplusStartup(ULONG_PTR *token, const GdiplusStartupInputC *input, void *output);
int WINAPI GdipCreateBitmapFromScan0(INT width, INT height, INT stride, INT format, BYTE *scan0, void **bitmap);
int WINAPI GdipCreateBitmapFromFile(const WCHAR *file, void **bitmap);
int WINAPI GdipGetImageGraphicsContext(void *image, void **graphics);
int WINAPI GdipDrawImageRectI(void *graphics, void *image, INT x, INT y, INT width, INT height);
int WINAPI GdipDeleteGraphics(void *graphics);
int WINAPI GdipSaveImageToFile(void *image, const WCHAR *file, const CLSID *encoder, const void *parameters);
int WINAPI GdipDisposeImage(void *image);

static const CLSID PNG_ENCODER = {0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};

static bool gdiplus_ready(void)
{
    static ULONG_PTR token;
    static int state; /* 0 untried, 1 up, -1 failed */
    if (state == 0)
    {
        GdiplusStartupInputC input = {1, NULL, FALSE, FALSE};
        state = GdiplusStartup(&token, &input, NULL) == 0 ? 1 : -1;
        if (state < 0)
            log_warn(plfMisc, "Card print: GDI+ unavailable, no card images");
    }
    return state > 0;
}

static const char *font_name(void)
{
    static bool loaded;
    if (!loaded)
    {
        loaded = true;
        const char *file = Config.wmmt3.print_font_file;
        if (file && *file)
        {
            if (AddFontResourceExA(file, FR_PRIVATE, NULL) > 0)
                log_info(plfMisc, "Card print: loaded font %s", file);
            else
                log_warn(plfMisc, "Card print: could not load %s, using an installed font", file);
        }
    }
    const char *name = Config.wmmt3.print_font_name;
    return name && *name ? name : "MS Gothic";
}

typedef struct
{
    HDC dc;
    HFONT fonts[2][2]; /* [double height][double width] */
} Pen;

static HFONT make_font(int doubleHeight, int doubleWidth)
{
    WCHAR face[LF_FACESIZE];
    MultiByteToWideChar(CP_ACP, 0, font_name(), -1, face, LF_FACESIZE);
    int height = DOTS(CHAR_HEIGHT_DOTS * (doubleHeight ? 2 : 1));
    int width = DOTS(HALF_WIDTH_DOTS * (doubleWidth ? 2 : 1));
    return CreateFontW(-height, width, 0, 0, doubleWidth ? FW_BOLD : FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET, 0, 0,
                       NONANTIALIASED_QUALITY, FIXED_PITCH, face);
}

static bool is_half_width(WCHAR c)
{
    return c < 0x100 || (c >= 0xFF61 && c <= 0xFF9F);
}

static void draw_block(Pen *pen, const uint8_t *text, size_t length)
{
    int top = TOP_DOTS;
    int column = 0;
    bool tall = false, wide = false;
    bool lineWasTall = false;

    for (size_t i = 0; i < length; i++)
    {
        uint8_t c = text[i];
        if (c == 0x0D)
        {
            top += LINE_PITCH_DOTS + (lineWasTall ? CHAR_HEIGHT_DOTS : 0);
            column = 0;
            lineWasTall = false;
            wide = false;
            continue;
        }
        if (c == 0x11 || c == 0x14)
        {
            tall = c == 0x11;
            wide = false;
            continue;
        }
        if (c == 0x1B)
        {
            /* ESC 's' n n: double width */
            wide = i + 3 < length && text[i + 1] == 's';
            i += 3;
            continue;
        }
        if (c < 0x20)
            continue;

        char mb[2] = {(char)c, 0};
        int mbLength = 1;
        if (((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) && i + 1 < length)
        {
            mb[1] = (char)text[++i];
            mbLength = 2;
        }
        WCHAR ch;
        if (MultiByteToWideChar(932, 0, mb, mbLength, &ch, 1) != 1)
            continue;

        int cells = (is_half_width(ch) ? 1 : 2) * (wide ? 2 : 1);
        if (tall)
            lineWasTall = true;

        SelectObject(pen->dc, pen->fonts[tall][wide]);
        int x = DOTS(LEFT_DOTS + column * HALF_WIDTH_DOTS);
        int y = DOTS(top);
        if (ch != L' ' && ch != 0x3000)
            TextOutW(pen->dc, x, y, &ch, 1);
        column += cells;
    }
}

static void render(const char *pngPath)
{
    if (!gdiplus_ready())
        return;

    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = IMAGE_WIDTH;
    info.bmiHeader.biHeight = -IMAGE_HEIGHT;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bitmap)
    {
        DeleteDC(dc);
        return;
    }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    memset(bits, 0xFF, IMAGE_WIDTH * IMAGE_HEIGHT * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));

    Pen pen = {dc, {{0}}};
    for (int t = 0; t < 2; t++)
        for (int w = 0; w < 2; w++)
            pen.fonts[t][w] = make_font(t, w);
    HGDIOBJ oldFont = SelectObject(dc, pen.fonts[0][0]);

    for (int b = 0; b < g_blockCount; b++)
        draw_block(&pen, g_blocks[b], g_blockLength[b]);
    GdiFlush();

    uint32_t *pixel = (uint32_t *)bits;
    for (int i = 0; i < IMAGE_WIDTH * IMAGE_HEIGHT; i++)
    {
        uint32_t whiteness = pixel[i] & 0xFF;
        uint32_t alpha = 255 - whiteness;
        pixel[i] = alpha ? (alpha << 24) | (INK_R << 16) | (INK_G << 8) | INK_B : 0;
    }

    SelectObject(dc, oldFont);
    for (int t = 0; t < 2; t++)
        for (int w = 0; w < 2; w++)
            DeleteObject(pen.fonts[t][w]);
    SelectObject(dc, oldBitmap);
    DeleteDC(dc);

    WCHAR widePath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, pngPath, -1, widePath, MAX_PATH);

    void *text = NULL;
    if (GdipCreateBitmapFromScan0(IMAGE_WIDTH, IMAGE_HEIGHT, IMAGE_WIDTH * 4, PIXEL_FORMAT_32BPP_ARGB, bits,
                                  &text) == 0)
    {
        void *output = text;
        void *card = NULL;
        const char *templatePath = Config.wmmt3.print_template;
        if (templatePath && *templatePath)
        {
            WCHAR wideTemplate[MAX_PATH];
            MultiByteToWideChar(CP_ACP, 0, templatePath, -1, wideTemplate, MAX_PATH);
            void *graphics = NULL;
            if (GdipCreateBitmapFromFile(wideTemplate, &card) == 0 &&
                GdipGetImageGraphicsContext(card, &graphics) == 0)
            {
                GdipDrawImageRectI(graphics, text, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT);
                GdipDeleteGraphics(graphics);
                output = card;
            }
            else
            {
                log_warn(plfMisc, "Card print: could not use the template %s", templatePath);
            }
        }

        if (GdipSaveImageToFile(output, widePath, &PNG_ENCODER, NULL) == 0)
            log_info(plfMisc, "Card print: wrote %s", pngPath);
        if (card)
            GdipDisposeImage(card);
        GdipDisposeImage(text);
    }
    DeleteObject(bitmap);
}

void card_print_begin(void)
{
    g_blockCount = 0;
}

void card_print_text(const char *cardPath, const uint8_t *data, size_t length)
{
    if (length < 3 || g_blockCount >= MAX_BLOCKS)
        return;
    size_t textLength = length - 3;
    if (textLength > MAX_BLOCK_BYTES)
        textLength = MAX_BLOCK_BYTES;
    memcpy(g_blocks[g_blockCount], data + 3, textLength);
    g_blockLength[g_blockCount] = textLength;
    g_blockCount++;

    char pngPath[MAX_PATH];
    snprintf(pngPath, sizeof(pngPath), "%s.png", cardPath);
    render(pngPath);
}
