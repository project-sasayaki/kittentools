/*
 * The print side of the rewrite card reader (Sanwa CR-S31 family).
 *
 * The reader keeps a print buffer. 7Ch lays text into it, and may then print the
 * buffer onto the card's printed face. Each card keeps, beside its card file:
 *   <card>.bin.face.png   the printed face, which comes back when the card is
 *                         inserted again
 *   <card>.bin.stock.txt  the card's stock: the artwork of the blank card
 *   <card>.bin.png        the face on the stock, to look at
 *
 * Wire layouts (from the game's own command builders, clCardPrinter::send*Command):
 *   78h  card type, print method, then four setting bytes not decoded here
 *   7Ah  glyph number (binary), 72 bytes of a 24x24 one-bit bitmap
 *   7Ch  '0' print / '1' lay out only, '0' clear / '1' keep the buffer,
 *        start line (binary, 1 = top), then the text
 *   7Dh  first line, last line (binary, first <= last)
 * Text control codes (reader manual): 0Dh new line, 11h double height,
 * 14h normal size, ESC 's' yy xx size ('1'..'4' each way), ESC 'g' n user glyph.
 */
#define WIN32_LEAN_AND_MEAN
#define _CRT_RAND_S /* rand_s */
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "namcoCardPrint.h"
#include "../config/config.h"
#include "../games/wmmt3/wmmt3Config.h"
#include "../log/log.h"

#define IMAGE_WIDTH 640
#define IMAGE_HEIGHT 1019
#define IMAGE_PIXELS (IMAGE_WIDTH * IMAGE_HEIGHT)

/* Room for a path built from two MAX_PATH names: a card path and a suffix, or a
   folder, a separator and a file name. */
#define PATH_CHARS (2 * MAX_PATH + 2)

/* The head prints 8 dots per mm; the 54 mm card is 432 dots across. */
#define PX_PER_DOT (640.0 / 432.0)
#define DOTS(n) ((int)((n) * PX_PER_DOT + 0.5))

/* The print area is 312 x 556 dots: 26 half-width columns of 12 dots and 20 lines
   of 28. Its place on the card and the line layout were measured on cards printed
   by a cabinet: a line is as many 28-dot rows as its tallest character needs, and
   every character on a line stands on the line's bottom edge. Double height taking
   two rows is measured; three and four rows for ESC s sizes 3 and 4 follow from it. */
#define HALF_WIDTH_DOTS 12
#define CHAR_HEIGHT_DOTS 24
#define LINE_PITCH_DOTS 28
#define LEFT_DOTS 60
#define TOP_DOTS 64
#define AREA_WIDTH_DOTS 312
#define AREA_HEIGHT_DOTS 556

#define GLYPH_DOTS 24
#define GLYPH_BYTES (GLYPH_DOTS * GLYPH_DOTS / 8)
#define MAX_TEXT_BYTES 246

#define CARD_TYPE_BLUE_LEUCO 0x32
#define PRINT_OVERWRITE '0'
#define PRINT_OVERPRINT '1'

/* Ink laid out but not printed yet, as coverage (0 none .. 255 full). */
static uint8_t g_buffer[IMAGE_PIXELS];
/* The printed face of the card in the reader: non-premultiplied ARGB, alpha 0
   where nothing is printed. */
static uint32_t g_face[IMAGE_PIXELS];
/* The card in the reader ("" when none) and the file name of its stock. */
static char g_cardPath[MAX_PATH];
static char g_stock[MAX_PATH];

/* Until a 78h says otherwise: WMMT's cards, laid over what is already printed. */
static uint8_t g_cardType = CARD_TYPE_BLUE_LEUCO;
static uint8_t g_printMethod = PRINT_OVERPRINT;

static uint8_t g_glyphs[256][GLYPH_BYTES];
static bool g_glyphRegistered[256];

/* ---- GDI+ (flat API, declared here as MinGW has no C header for it) ---- */

typedef struct
{
    UINT32 version;
    void *debugCallback;
    BOOL suppressBackgroundThread;
    BOOL suppressExternalCodecs;
} GdiplusStartupInputC;

typedef struct
{
    INT X, Y, Width, Height;
} GpRectC;

typedef struct
{
    UINT Width;
    UINT Height;
    INT Stride;
    INT PixelFormat;
    void *Scan0;
    UINT_PTR Reserved;
} BitmapDataC;

#define PIXEL_FORMAT_32BPP_ARGB 0x26200A
#define IMAGE_LOCK_MODE_READ 1

int WINAPI GdiplusStartup(ULONG_PTR *token, const GdiplusStartupInputC *input, void *output);
int WINAPI GdipCreateBitmapFromScan0(INT width, INT height, INT stride, INT format, BYTE *scan0, void **bitmap);
int WINAPI GdipCreateBitmapFromFile(const WCHAR *file, void **bitmap);
int WINAPI GdipGetImageGraphicsContext(void *image, void **graphics);
int WINAPI GdipDrawImageRectI(void *graphics, void *image, INT x, INT y, INT width, INT height);
int WINAPI GdipDeleteGraphics(void *graphics);
int WINAPI GdipSaveImageToFile(void *image, const WCHAR *file, const CLSID *encoder, const void *parameters);
int WINAPI GdipDisposeImage(void *image);
int WINAPI GdipBitmapLockBits(void *bitmap, const GpRectC *rect, UINT flags, INT format, BitmapDataC *data);
int WINAPI GdipBitmapUnlockBits(void *bitmap, BitmapDataC *data);

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

static void to_wide(const char *text, WCHAR *out, int outLength)
{
    MultiByteToWideChar(CP_ACP, 0, text, -1, out, outLength);
}

static void *load_bitmap(const char *path)
{
    WCHAR widePath[PATH_CHARS];
    to_wide(path, widePath, PATH_CHARS);
    void *bitmap = NULL;
    return GdipCreateBitmapFromFile(widePath, &bitmap) == 0 ? bitmap : NULL;
}

static bool save_bitmap(void *bitmap, const char *path)
{
    WCHAR widePath[PATH_CHARS];
    to_wide(path, widePath, PATH_CHARS);
    return GdipSaveImageToFile(bitmap, widePath, &PNG_ENCODER, NULL) == 0;
}

/* ---- Small helpers ---- */

/* The part of a path after its last \ or /. */
static const char *file_name_of(const char *path)
{
    const char *name = path + strlen(path);
    while (name > path && name[-1] != '\\' && name[-1] != '/')
        name--;
    return name;
}

static void warn_once(bool *warned, const char *what, unsigned value)
{
    if (!*warned)
    {
        *warned = true;
        log_warn(plfMisc, "Card print: %s %02X is not supported, ignored", what, value);
    }
}

static const char *font_name(void)
{
    static bool loaded;
    if (!loaded)
    {
        loaded = true;
        const char *file = Wmmt3Config.print_font_file;
        if (file && *file)
        {
            if (AddFontResourceExA(file, FR_PRIVATE, NULL) > 0)
                log_info(plfMisc, "Card print: loaded font %s", file);
            else
                log_warn(plfMisc, "Card print: could not load %s, using an installed font", file);
        }
    }
    const char *name = Wmmt3Config.print_font_name;
    return name && *name ? name : "MS Gothic";
}

/* ---- Reading the text of a 7Ch ---- */

typedef struct
{
    int tall; /* 1..4 */
    int wide; /* 1..4 */
} CharSize;

static const CharSize NORMAL_SIZE = {1, 1};

typedef enum
{
    TOKEN_END,
    TOKEN_NEW_LINE,
    TOKEN_CHAR,
    TOKEN_GLYPH,
    TOKEN_NONE /* a control code: it may have changed the size */
} TokenKind;

typedef struct
{
    TokenKind kind;
    WCHAR ch;       /* TOKEN_CHAR */
    uint8_t glyph;  /* TOKEN_GLYPH */
    int halfWidths; /* columns taken at size 1 */
} Token;

static bool is_lead_byte(uint8_t c)
{
    return (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC);
}

static bool is_half_width(WCHAR c)
{
    return c < 0x100 || (c >= 0xFF61 && c <= 0xFF9F);
}

static bool is_size_digit(uint8_t c)
{
    return c >= '1' && c <= '4';
}

/* Read one item at text[*i], moving *i past it and applying size changes to *size. */
static Token next_token(const uint8_t *text, size_t length, size_t *i, CharSize *size)
{
    static bool warnedEscape, warnedSize;
    Token token = {TOKEN_NONE, 0, 0, 0};
    if (*i >= length)
    {
        token.kind = TOKEN_END;
        return token;
    }

    uint8_t c = text[(*i)++];
    if (c == 0x0D)
    {
        token.kind = TOKEN_NEW_LINE;
    }
    else if (c == 0x11)
    {
        size->tall = 2;
        size->wide = 1;
    }
    else if (c == 0x14)
    {
        *size = NORMAL_SIZE;
    }
    else if (c == 0x1B && *i < length)
    {
        uint8_t kind = text[(*i)++];
        if (kind == 's' && *i + 2 <= length)
        {
            uint8_t tall = text[(*i)++], wide = text[(*i)++];
            if (is_size_digit(tall) && is_size_digit(wide))
            {
                size->tall = tall - '0';
                size->wide = wide - '0';
            }
            else if (!warnedSize)
            {
                warnedSize = true;
                log_warn(plfMisc, "Card print: character size %02X %02X is not supported, ignored", tall, wide);
            }
        }
        else if (kind == 'g' && *i < length)
        {
            token.kind = TOKEN_GLYPH;
            token.glyph = text[(*i)++];
            token.halfWidths = 2;
        }
        else
        {
            warn_once(&warnedEscape, "escape sequence", kind);
        }
    }
    else if (c >= 0x20)
    {
        char mb[2] = {(char)c, 0};
        int mbLength = 1;
        if (is_lead_byte(c) && *i < length)
        {
            mb[1] = (char)text[(*i)++];
            mbLength = 2;
        }
        if (MultiByteToWideChar(932, 0, mb, mbLength, &token.ch, 1) == 1)
        {
            token.kind = TOKEN_CHAR;
            token.halfWidths = is_half_width(token.ch) ? 1 : 2;
        }
    }
    return token;
}

/* Rows taken by the line starting at text[start]: those of its tallest character. */
static int line_rows(const uint8_t *text, size_t length, size_t start, CharSize size)
{
    int rows = 1;
    size_t i = start;
    for (;;)
    {
        Token token = next_token(text, length, &i, &size);
        if (token.kind == TOKEN_END || token.kind == TOKEN_NEW_LINE)
            return rows;
        if ((token.kind == TOKEN_CHAR || token.kind == TOKEN_GLYPH) && size.tall > rows)
            rows = size.tall;
    }
}

/* ---- Laying text into the buffer ---- */

/* A device context to draw text into, with a font per character size. */
typedef struct
{
    HDC dc;
    HFONT fonts[4][4]; /* [tall - 1][wide - 1], made when first needed */
} Canvas;

static HFONT canvas_font(Canvas *canvas, CharSize size)
{
    HFONT *font = &canvas->fonts[size.tall - 1][size.wide - 1];
    if (!*font)
    {
        WCHAR face[LF_FACESIZE];
        to_wide(font_name(), face, LF_FACESIZE);
        int height = DOTS(CHAR_HEIGHT_DOTS * size.tall);
        int width = DOTS(HALF_WIDTH_DOTS * size.wide);
        /* Widened characters print heavier on the card. */
        *font = CreateFontW(-height, width, 0, 0, size.wide > 1 ? FW_BOLD : FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET, 0,
                            0, NONANTIALIASED_QUALITY, FIXED_PITCH, face);
    }
    return *font;
}

/* Each set bit of the 24x24 glyph becomes a block of size.wide x size.tall dots. */
static void draw_glyph(Canvas *canvas, uint8_t number, int leftDots, int topDots, CharSize size)
{
    static bool warnedMissing;
    if (!g_glyphRegistered[number])
    {
        warn_once(&warnedMissing, "unregistered user glyph", number);
        return;
    }
    const uint8_t *bits = g_glyphs[number];
    HBRUSH ink = (HBRUSH)GetStockObject(BLACK_BRUSH);
    for (int y = 0; y < GLYPH_DOTS; y++)
        for (int x = 0; x < GLYPH_DOTS; x++)
        {
            if (!(bits[y * 3 + x / 8] & (0x80 >> (x % 8))))
                continue;
            int dotX = leftDots + x * size.wide, dotY = topDots + y * size.tall;
            RECT r = {DOTS(dotX), DOTS(dotY), DOTS(dotX + size.wide), DOTS(dotY + size.tall)};
            FillRect(canvas->dc, &r, ink);
        }
}

static void draw_text(Canvas *canvas, const uint8_t *text, size_t length, int firstLine)
{
    CharSize size = NORMAL_SIZE;
    size_t i = 0;
    int column = 0;
    /* A tall line also takes the rows above its bottom row. */
    int row = firstLine - 1 + line_rows(text, length, i, size) - 1;

    for (;;)
    {
        Token token = next_token(text, length, &i, &size);
        if (token.kind == TOKEN_END)
            break;
        if (token.kind == TOKEN_NEW_LINE)
        {
            column = 0;
            row += line_rows(text, length, i, size);
            continue;
        }
        if (token.kind != TOKEN_CHAR && token.kind != TOKEN_GLYPH)
            continue;

        int left = LEFT_DOTS + column * HALF_WIDTH_DOTS;
        int bottom = TOP_DOTS + row * LINE_PITCH_DOTS + CHAR_HEIGHT_DOTS;
        int top = bottom - CHAR_HEIGHT_DOTS * size.tall;
        if (token.kind == TOKEN_GLYPH)
        {
            draw_glyph(canvas, token.glyph, left, top, size);
        }
        else if (token.ch != L' ' && token.ch != 0x3000)
        {
            SelectObject(canvas->dc, canvas_font(canvas, size));
            TextOutW(canvas->dc, DOTS(left), DOTS(top), &token.ch, 1);
        }
        column += token.halfWidths * size.wide;
    }
}

/* Lay the text into the buffer: draw it black on white, clipped to the print
   area, and add the result as coverage. */
static void lay_out(const uint8_t *text, size_t length, int firstLine)
{
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
        log_warn(plfMisc, "Card print: no memory to lay out text");
        return;
    }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    memset(bits, 0xFF, IMAGE_PIXELS * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    IntersectClipRect(dc, DOTS(LEFT_DOTS), DOTS(TOP_DOTS), DOTS(LEFT_DOTS + AREA_WIDTH_DOTS),
                      DOTS(TOP_DOTS + AREA_HEIGHT_DOTS));

    Canvas canvas = {dc, {{0}}};
    HGDIOBJ oldFont = GetCurrentObject(dc, OBJ_FONT);
    draw_text(&canvas, text, length, firstLine);
    GdiFlush();

    const uint32_t *pixel = (const uint32_t *)bits;
    for (int i = 0; i < IMAGE_PIXELS; i++)
    {
        uint8_t coverage = (uint8_t)(255 - (pixel[i] & 0xFF));
        if (coverage > g_buffer[i])
            g_buffer[i] = coverage;
    }

    SelectObject(dc, oldFont);
    for (int t = 0; t < 4; t++)
        for (int w = 0; w < 4; w++)
            if (canvas.fonts[t][w])
                DeleteObject(canvas.fonts[t][w]);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

/* ---- The card's stock ----
   print_template names one image, or a folder of them. A card gets its stock when
   it comes out of the dispenser (a renewed card is a new card too), drawn at random
   from the folder as the cabinet's card stack would give it, and keeps it for good:
   <card>.bin.stock.txt records the image's file name, looked up in that folder. */

static const char *template_setting(void)
{
    const char *setting = Wmmt3Config.print_template;
    return setting && *setting ? setting : NULL;
}

static bool template_is_folder(void)
{
    DWORD attributes = GetFileAttributesA(template_setting());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

/* The folder card stock is looked up in: the setting itself, or the image's folder. */
static void stock_folder(char *out, size_t outSize)
{
    const char *setting = template_setting();
    if (template_is_folder())
    {
        snprintf(out, outSize, "%s", setting);
        return;
    }
    const char *name = file_name_of(setting);
    if (name == setting)
        snprintf(out, outSize, ".");
    else /* up to, not including, the separator before the name */
        snprintf(out, outSize, "%.*s", (int)(name - setting - 1), setting);
}

static bool is_image_name(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (!_stricmp(dot, ".png") || !_stricmp(dot, ".jpg") || !_stricmp(dot, ".jpeg") ||
                   !_stricmp(dot, ".bmp"));
}

/* Count the images in the folder, copying the name of the index-th one (if any). */
static int find_images(const char *folder, int index, char *name, size_t nameSize)
{
    char pattern[PATH_CHARS];
    snprintf(pattern, sizeof(pattern), "%s\\*", folder);
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE)
        return 0;
    int count = 0;
    do
    {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !is_image_name(found.cFileName))
            continue;
        if (count == index)
            snprintf(name, nameSize, "%s", found.cFileName);
        count++;
    } while (FindNextFileA(search, &found));
    FindClose(search);
    return count;
}

static void stock_record_path(char *out, size_t outSize)
{
    snprintf(out, outSize, "%s.stock.txt", g_cardPath);
}

static void read_stock(void)
{
    g_stock[0] = 0;
    char path[PATH_CHARS];
    stock_record_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    if (fgets(g_stock, sizeof(g_stock), f))
        g_stock[strcspn(g_stock, "\r\n")] = 0;
    fclose(f);
}

/* Give the card its stock and record it. Without print_template the card has none. */
static void assign_stock(void)
{
    g_stock[0] = 0;
    if (!template_setting())
        return;

    if (template_is_folder())
    {
        char folder[MAX_PATH];
        stock_folder(folder, sizeof(folder));
        int count = find_images(folder, -1, NULL, 0);
        if (count == 0)
        {
            log_warn(plfMisc, "Card print: no card images in %s", folder);
            return;
        }
        unsigned int draw = 0;
        rand_s(&draw);
        find_images(folder, (int)(draw % (unsigned int)count), g_stock, sizeof(g_stock));
    }
    else
    {
        snprintf(g_stock, sizeof(g_stock), "%s", file_name_of(template_setting()));
    }

    char path[PATH_CHARS];
    stock_record_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f)
    {
        log_warn(plfMisc, "Card print: could not write %s", path);
        return;
    }
    fprintf(f, "%s\n", g_stock);
    fclose(f);
    log_info(plfMisc, "Card print: %s is printed on %s", g_cardPath, g_stock);
}

/* ---- The card's face ---- */

static void face_path(char *out, size_t outSize)
{
    snprintf(out, outSize, "%s.face.png", g_cardPath);
}

/* Bring back the face this card was last printed with. */
static void load_face(void)
{
    char path[PATH_CHARS];
    face_path(path, sizeof(path));
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES || !gdiplus_ready())
        return;

    void *bitmap = load_bitmap(path);
    if (!bitmap)
    {
        log_warn(plfMisc, "Card print: could not read %s, the card's face starts blank", path);
        return;
    }
    GpRectC rect = {0, 0, IMAGE_WIDTH, IMAGE_HEIGHT};
    BitmapDataC data;
    if (GdipBitmapLockBits(bitmap, &rect, IMAGE_LOCK_MODE_READ, PIXEL_FORMAT_32BPP_ARGB, &data) == 0)
    {
        for (int y = 0; y < IMAGE_HEIGHT; y++)
            memcpy(g_face + y * IMAGE_WIDTH, (const uint8_t *)data.Scan0 + y * data.Stride, IMAGE_WIDTH * 4);
        GdipBitmapUnlockBits(bitmap, &data);
        log_info(plfMisc, "Card print: the card's printed face is %s", path);
    }
    else
    {
        log_warn(plfMisc, "Card print: %s is not %ux%u, the card's face starts blank", path, IMAGE_WIDTH,
                 IMAGE_HEIGHT);
    }
    GdipDisposeImage(bitmap);
}

/* Write <card>.bin.png: the face drawn on the card's stock, or on its own
   (transparent) when the card has no stock. */
static void write_card_image(void *face)
{
    /* A card made before it had a recorded stock gets one on its first print. */
    if (!g_stock[0])
        assign_stock();

    void *output = face;
    void *stock = NULL;
    if (g_stock[0] && template_setting())
    {
        char folder[MAX_PATH], stockPath[PATH_CHARS];
        stock_folder(folder, sizeof(folder));
        snprintf(stockPath, sizeof(stockPath), "%s\\%s", folder, g_stock);
        void *graphics = NULL;
        stock = load_bitmap(stockPath);
        if (stock && GdipGetImageGraphicsContext(stock, &graphics) == 0)
        {
            GdipDrawImageRectI(graphics, face, 0, 0, IMAGE_WIDTH, IMAGE_HEIGHT);
            GdipDeleteGraphics(graphics);
            output = stock;
        }
        else
        {
            log_warn(plfMisc, "Card print: the card's stock %s is missing, printed without it", stockPath);
        }
    }

    char path[PATH_CHARS];
    snprintf(path, sizeof(path), "%s.png", g_cardPath);
    if (save_bitmap(output, path))
        log_info(plfMisc, "Card print: wrote %s", path);
    if (stock)
        GdipDisposeImage(stock);
}

/* Keep the face beside the card file, and update the card's picture. */
static void store_face(void)
{
    if (!g_cardPath[0] || !gdiplus_ready())
        return;

    void *face = NULL;
    if (GdipCreateBitmapFromScan0(IMAGE_WIDTH, IMAGE_HEIGHT, IMAGE_WIDTH * 4, PIXEL_FORMAT_32BPP_ARGB,
                                  (BYTE *)g_face, &face) != 0)
        return;

    char path[PATH_CHARS];
    face_path(path, sizeof(path));
    if (!save_bitmap(face, path))
        log_warn(plfMisc, "Card print: could not write %s", path);
    write_card_image(face);
    GdipDisposeImage(face);
}

static uint32_t ink_colour(void)
{
    switch (g_cardType)
    {
        case 0x32: /* leuco rewrite, blue */
        case 0x37:
            return 0x606094u;
        case 0x30: /* white-turbid rewrite: the print shows white */
        case 0x35:
        case 0x36:
            return 0xF0F0F0u;
        default: /* leuco rewrite, black; thermal */
            return 0x202020u;
    }
}

/* Print the buffer onto the face. Overwrite printing (rewrite cards) erases the
   old print as it goes, so the face becomes the buffer; overprinting adds to it. */
static void print_buffer(void)
{
    uint32_t colour = ink_colour();
    bool overwrite = g_printMethod == PRINT_OVERWRITE;
    for (int i = 0; i < IMAGE_PIXELS; i++)
    {
        uint32_t ink = g_buffer[i];
        if (ink)
            g_face[i] = ink << 24 | colour;
        else if (overwrite)
            g_face[i] = 0;
    }
    store_face();
}

/* ---- Commands ---- */

void card_print_card_inserted(const char *cardPath, bool newCard)
{
    snprintf(g_cardPath, sizeof(g_cardPath), "%s", cardPath);
    memset(g_face, 0, sizeof(g_face));
    if (newCard)
    {
        assign_stock();
    }
    else
    {
        read_stock();
        load_face();
    }
}

void card_print_card_removed(void)
{
    g_cardPath[0] = 0;
    g_stock[0] = 0;
}

void card_print_settings(const uint8_t *data, size_t length)
{
    static bool warnedSettings;
    if (length < 2)
        return;
    g_cardType = data[0];
    g_printMethod = data[1];
    for (size_t i = 2; i < length; i++)
        if (data[i] != '0')
            warn_once(&warnedSettings, "print setting", data[i]);
}

void card_print_register_glyph(const uint8_t *data, size_t length)
{
    if (length < 1 + GLYPH_BYTES)
        return;
    memcpy(g_glyphs[data[0]], data + 1, GLYPH_BYTES);
    g_glyphRegistered[data[0]] = true;
}

void card_print_text(const uint8_t *data, size_t length)
{
    if (length < 3)
        return;
    bool printNow = data[0] == '0';
    bool clearBuffer = data[1] == '0';
    int firstLine = data[2];

    const uint8_t *text = data + 3;
    size_t textLength = length - 3;
    const uint8_t *nul = memchr(text, 0, textLength);
    if (nul)
        textLength = (size_t)(nul - text);
    if (textLength > MAX_TEXT_BYTES)
        textLength = MAX_TEXT_BYTES;

    if (clearBuffer)
        memset(g_buffer, 0, sizeof(g_buffer));
    lay_out(text, textLength, firstLine);
    if (printNow && g_cardPath[0])
        print_buffer();
}

/* Erase whole print lines. The game allows lines 1..23 here, more than the 20 a
   7Ch can start on; lines are taken as 28-dot print lines, which is not verified
   on a cabinet, and erasing stops at the print area. */
void card_print_erase(const uint8_t *data, size_t length)
{
    if (length < 2 || data[0] < 1 || data[0] > data[1] || !g_cardPath[0])
        return;
    int topDots = TOP_DOTS + (data[0] - 1) * LINE_PITCH_DOTS;
    int bottomDots = TOP_DOTS + data[1] * LINE_PITCH_DOTS;
    if (bottomDots > TOP_DOTS + AREA_HEIGHT_DOTS)
        bottomDots = TOP_DOTS + AREA_HEIGHT_DOTS;
    int left = DOTS(LEFT_DOTS), right = DOTS(LEFT_DOTS + AREA_WIDTH_DOTS);
    for (int y = DOTS(topDots); y < DOTS(bottomDots) && y < IMAGE_HEIGHT; y++)
        memset(g_face + y * IMAGE_WIDTH + left, 0, (size_t)(right - left) * 4);
    store_face();
}
