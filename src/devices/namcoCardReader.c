#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "namcoCardReader.h"
#include "namcoCardPrint.h"
#include "../config/config.h"
#include "../log/log.h"

#define STX 0x02
#define ETX 0x03
#define ENQ 0x05
#define ACK 0x06
#define LINUX_FIONREAD 0x541B

#define TRACK_SIZE 69
#define TRACKS 3

static CRITICAL_SECTION g_lock;
static bool g_ready;

static uint8_t g_input[1024];
static size_t g_inputLength;
static uint8_t g_output[1024];
static size_t g_outputLength;

static uint8_t g_command;
static uint8_t g_commandData[256];
static size_t g_commandDataLength;
static bool g_haveCommand;

static bool g_cardInside;
static uint8_t g_tracks[TRACKS * TRACK_SIZE];
static char g_cardPath[MAX_PATH];
static bool g_insertKeyWasDown;
static volatile LONG g_insertButtonPressed;

void namcoCardReaderRequestInsert(void)
{
    LONG now = (LONG)GetTickCount();
    InterlockedExchange(&g_insertButtonPressed, now ? now : 1);
    log_info(plfMisc, "Card reader: insert pressed%s", g_cardInside ? " (a card is already inside)" : "");
}
static bool g_insertRequested;

/* 38400 8E1: eleven bits per byte */
#define BYTE_MS (11.0 * 1000.0 / 38400.0)

/* Estimates, not measured on a cabinet. */
#define INSERT_MS 1200
#define READ_MS 500
#define WRITE_MS 700
#define ERASE_MS 1500
#define PRINT_LINE_MS 250
#define EJECT_MS 800
#define DISPENSE_MS 1500
#define INIT_MS 500

static bool g_cardFresh;
static bool g_jobStarted;
static double g_jobEnd;
static double g_lineFreeAt;
static double g_rxDoneAt;
static double g_outputAt[1024];

static bool timing(void)
{
    return Config.wmmt3.card_speed_emulation;
}

static double now_ms(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;
    if (!freq.QuadPart)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)freq.QuadPart;
}

static size_t output_arrived(void)
{
    if (!timing())
        return g_outputLength;
    double now = now_ms();
    size_t n = 0;
    while (n < g_outputLength && g_outputAt[n] <= now)
        n++;
    return n;
}

static void lock(void)
{
    if (!g_ready)
    {
        InitializeCriticalSection(&g_lock);
        g_ready = true;
    }
    EnterCriticalSection(&g_lock);
}

static void unlock(void)
{
    LeaveCriticalSection(&g_lock);
}

static const char *card_dir(void)
{
    return Config.wmmt3.card_dir && *Config.wmmt3.card_dir ? Config.wmmt3.card_dir : "cards";
}

static void card_path(const char *name, char *out, size_t outSize)
{
    snprintf(out, outSize, "%s\\%s", card_dir(), name);
}

static void default_card_path(void)
{
    card_path(Config.wmmt3.card_file ? Config.wmmt3.card_file : "card.bin", g_cardPath, sizeof(g_cardPath));
}

static bool load_card(void)
{
    default_card_path();
    FILE *f = fopen(g_cardPath, "rb");
    if (!f)
    {
        log_warn(plfMisc, "Card reader: no card file at %s", g_cardPath);
        return false;
    }
    memset(g_tracks, 0, sizeof(g_tracks));
    size_t n = fread(g_tracks, 1, sizeof(g_tracks), f);
    fclose(f);
    log_info(plfMisc, "Card reader: card %s inserted (%u bytes)", g_cardPath, (unsigned)n);
    return true;
}

static void save_card(void)
{
    _mkdir(card_dir());
    FILE *f = fopen(g_cardPath, "wb");
    if (!f)
    {
        log_error(plfMisc, "Card reader: could not save %s", g_cardPath);
        return;
    }
    fwrite(g_tracks, 1, sizeof(g_tracks), f);
    fclose(f);
    log_info(plfMisc, "Card reader: saved %s", g_cardPath);
}

static void dispense_card(void)
{
    default_card_path();
    if (GetFileAttributesA(g_cardPath) != INVALID_FILE_ATTRIBUTES)
    {
        char name[64];
        time_t now = time(NULL);
        strftime(name, sizeof(name), "card_%Y%m%d_%H%M%S.bin", localtime(&now));
        card_path(name, g_cardPath, sizeof(g_cardPath));
    }
    memset(g_tracks, 0, sizeof(g_tracks));
    g_cardInside = true;
    log_info(plfMisc, "Card reader: dispensed a new card as %s", g_cardPath);
}

/* 0..2 one track, 3..5 two, 6 all three */
static size_t track_span(int id, size_t *first)
{
    static const struct { uint8_t first, count; } spans[] = {
        {0, 1}, {1, 1}, {2, 1}, {0, 2}, {0, 2}, {1, 2}, {0, 3},
    };
    if (id < 0 || id > 6)
        id = 6;
    *first = (size_t)spans[id].first * TRACK_SIZE;
    return (size_t)spans[id].count * TRACK_SIZE;
}

static void record_print(uint8_t command, const uint8_t *data, size_t length)
{
    if (command == 0x78 || command == 0x7D)
        card_print_begin();
    else if (command == 0x7C && g_cardPath[0])
        card_print_text(g_cardPath, data, length);
}

static void respond(uint8_t command, char r, char p, char s, const uint8_t *data, size_t length);
static char card_position(void);

static void queue(const uint8_t *bytes, size_t length)
{
    if (g_outputLength + length > sizeof(g_output))
        g_outputLength = 0;
    memcpy(g_output + g_outputLength, bytes, length);
    if (timing())
    {
        double at = g_lineFreeAt > g_rxDoneAt ? g_lineFreeAt : g_rxDoneAt;
        double now = now_ms();
        if (at < now)
            at = now;
        for (size_t i = 0; i < length; i++)
        {
            at += BYTE_MS;
            g_outputAt[g_outputLength + i] = at;
        }
        g_lineFreeAt = at;
    }
    g_outputLength += length;
}

static bool busy_for(double ms)
{
    if (!timing() || ms <= 0)
        return false;
    double now = now_ms();
    if (!g_jobStarted)
    {
        g_jobStarted = true;
        g_jobEnd = now + ms;
    }
    if (now < g_jobEnd)
    {
        respond(g_command, card_position(), '0', '3', NULL, 0);
        return true;
    }
    return false;
}

static void respond(uint8_t command, char r, char p, char s, const uint8_t *data, size_t length)
{
    uint8_t frame[300];
    size_t at = 0;
    frame[at++] = STX;
    frame[at++] = (uint8_t)(length + 6);
    frame[at++] = command;
    frame[at++] = (uint8_t)r;
    frame[at++] = (uint8_t)p;
    frame[at++] = (uint8_t)s;
    if (length)
        memcpy(frame + at, data, length);
    at += length;
    frame[at++] = ETX;
    uint8_t bcc = 0;
    for (size_t i = 1; i < at; i++)
        bcc ^= frame[i];
    frame[at++] = bcc;
    queue(frame, at);
}

static char card_position(void)
{
    return g_cardInside ? '1' : '0';
}

static bool try_insert(void)
{
    if (g_cardInside)
        return true;

    static DWORD lastAsk;
    DWORD now = GetTickCount();
    if (!Config.wmmt3.card_auto_insert && (lastAsk == 0 || now - lastAsk > 2000))
        log_debug(plfMisc, "Card reader: the game is waiting for a card");
    lastAsk = now;

    bool keyDown = Config.wmmt3.card_key && (GetAsyncKeyState(Config.wmmt3.card_key) & 0x8000);
    bool pressed = keyDown && !g_insertKeyWasDown;
    g_insertKeyWasDown = keyDown;
    LONG pressedAt = InterlockedExchange(&g_insertButtonPressed, 0);
    if (pressedAt && (DWORD)GetTickCount() - (DWORD)pressedAt < 1000)
        pressed = true;

    if ((pressed || Config.wmmt3.card_auto_insert) && load_card())
    {
        g_cardInside = true;
        g_cardFresh = true;
    }
    return g_cardInside;
}

static void answer_enquiry(void)
{
    if (g_insertRequested && !g_cardInside)
    {
        static unsigned traced;
        if (traced < 12)
        {
            traced++;
            log_debug(plfMisc, "Card reader: game sent %02X while waiting for a card",
                     g_haveCommand ? g_command : 0x05);
        }
        if (try_insert())
            log_info(plfMisc, "Card reader: card inserted by the button");
    }

    if (!g_haveCommand)
    {
        respond(0x20, card_position(), '0', '0', NULL, 0);
        return;
    }

    const uint8_t *d = g_commandData;
    size_t n = g_commandDataLength;

    switch (g_command)
    {
        case 0x33: /* read */
        {
            if (!g_cardInside)
            {
                if (n >= 1 && d[0] == '2')
                {
                    g_insertRequested = true;
                    if (!try_insert())
                    {
                        /* Done, no card: a busy reply makes requestInsertCancel() refuse and NO hangs. */
                        respond(0x33, '0', '0', '0', NULL, 0);
                        break;
                    }
                }
                else
                {
                    respond(0x33, '0', '0', '0', NULL, 0);
                    break;
                }
            }
            if (busy_for(g_cardFresh ? INSERT_MS : READ_MS))
                return;
            g_cardFresh = false;
            g_insertRequested = false;
            size_t first;
            size_t length = track_span(n >= 3 ? d[2] - '0' : 6, &first);
            respond(0x33, card_position(), '0', '0', g_tracks + first, length);
            break;
        }

        case 0x53: /* write */
        {
            if (!g_cardInside || n < 3)
            {
                respond(0x53, card_position(), '2', '0', NULL, 0);
                break;
            }
            if (busy_for(WRITE_MS))
                return;
            size_t first;
            size_t length = track_span(d[2] - '0', &first);
            if (n - 3 < length)
                length = n - 3;
            memcpy(g_tracks + first, d + 3, length);
            save_card();
            respond(0x53, card_position(), '0', '0', NULL, 0);
            break;
        }

        case 0x80: /* eject */
            if (g_cardInside && busy_for(EJECT_MS))
                return;
            if (g_cardInside)
                log_info(plfMisc, "Card reader: card ejected");
            g_cardInside = false;
            respond(0x80, '0', '0', '0', NULL, 0);
            break;

        case 0xB0: /* dispenser */
            if (n >= 1 && d[0] == '1')
            {
                if (busy_for(DISPENSE_MS))
                    return;
                dispense_card();
                g_cardFresh = false;
            }
            respond(0xB0, card_position(), '0', '0', NULL, 0);
            break;

        case 0x40: /* cancel: answered "no job", as the real board does */
            g_insertRequested = false;
            respond(0x40, card_position(), '0', '0', NULL, 0);
            break;

        case 0x10: /* init */
            if (busy_for(INIT_MS))
                return;
            respond(g_command, card_position(), '0', '0', NULL, 0);
            break;

        case 0x20: /* status */
        case 0xA0: /* cleaning */
        case 0xF5: /* battery */
            respond(g_command, card_position(), '0', '0', NULL, 0);
            break;

        case 0x7C: /* print a line */
        case 0x7D: /* erase the printed face */
            if (g_cardInside && busy_for(g_command == 0x7C ? PRINT_LINE_MS : ERASE_MS))
                return;
            respond(g_command, card_position(), '0', '0', NULL, 0);
            break;

        case 0x78: /* print settings */
        case 0x7A: /* user-defined glyphs */
            respond(g_command, card_position(), '0', '0', NULL, 0);
            break;

        default:
            log_warn(plfMisc, "Card reader: command %02X not supported", g_command);
            respond(g_command, card_position(), '0', '2', NULL, 0);
            break;
    }
    g_haveCommand = false;
}

static void accept_command(const uint8_t *frame, size_t length)
{
    g_command = frame[2];
    g_commandDataLength = length - 8; /* STX LEN CMD 3x00 ... ETX BCC */
    memcpy(g_commandData, frame + 6, g_commandDataLength);
    g_haveCommand = true;
    g_jobStarted = false;

    static unsigned long commands;
    if (commands++ < 64)
        log_debug(plfMisc, "Card reader: command %02X with %u data bytes", g_command, (unsigned)g_commandDataLength);

    record_print(g_command, g_commandData, g_commandDataLength);

    uint8_t ack = ACK;
    queue(&ack, 1);
}

static void consume_input(void)
{
    for (;;)
    {
        if (g_inputLength == 0)
            return;

        size_t used = 1;
        if (g_input[0] == ENQ)
        {
            answer_enquiry();
        }
        else if (g_input[0] == STX)
        {
            if (g_inputLength < 2)
                return;
            size_t total = (size_t)g_input[1] + 2;
            if (total < 8)
            {
                used = 1;
            }
            else
            {
                if (g_inputLength < total)
                    return;
                uint8_t bcc = 0;
                for (size_t i = 1; i + 1 < total; i++)
                    bcc ^= g_input[i];
                if (bcc == g_input[total - 1])
                    accept_command(g_input, total);
                else
                    log_warn(plfMisc, "Card reader: dropped a frame with a bad BCC");
                used = total;
            }
        }
        memmove(g_input, g_input + used, g_inputLength - used);
        g_inputLength -= used;
    }
}

int namcoCardReaderInit(void)
{
    lock();
    g_inputLength = g_outputLength = 0;
    g_haveCommand = false;
    unlock();
    log_info(plfMisc, "Card reader: attached on /dev/ttyM2 (cards in %s)",
             card_dir());
    return 0;
}

ssize_t namcoCardReaderRead(int fd, void *buf, size_t count)
{
    (void)fd;
    lock();
    size_t arrived = output_arrived();
    if (arrived == 0)
    {
        unlock();
        errno = EAGAIN;
        return -1;
    }
    size_t taken = count < arrived ? count : arrived;
    memcpy(buf, g_output, taken);
    memmove(g_output, g_output + taken, g_outputLength - taken);
    memmove(g_outputAt, g_outputAt + taken, (g_outputLength - taken) * sizeof(g_outputAt[0]));
    g_outputLength -= taken;
    unlock();
    /* The game latches any unexpected errno. */
    errno = 0;
    return (ssize_t)taken;
}

ssize_t namcoCardReaderWrite(int fd, const void *buf, size_t count)
{
    (void)fd;
    lock();
    if (g_inputLength + count > sizeof(g_input))
        g_inputLength = 0;
    if (count <= sizeof(g_input))
    {
        memcpy(g_input + g_inputLength, buf, count);
        g_inputLength += count;
    }
    if (timing())
    {
        double now = now_ms();
        g_rxDoneAt = (g_rxDoneAt > now ? g_rxDoneAt : now) + (double)count * BYTE_MS;
    }
    consume_input();
    unlock();
    /* The game latches any unexpected errno. */
    errno = 0;
    return (ssize_t)count;
}

int namcoCardReaderIoctl(int fd, unsigned long request, void *data)
{
    (void)fd;
    if (request == LINUX_FIONREAD && data)
    {
        lock();
        *(int *)data = (int)output_arrived();
        unlock();
    }
    return 0;
}

int namcoCardReaderClose(int fd)
{
    (void)fd;
    lock();
    g_inputLength = g_outputLength = 0;
    unlock();
    return 0;
}
