/* clKickback this->0x2c: 0 idle, 1 motor stopped, 3 reply owed, 4 latched E20.
 * One E00 per frame; volunteered replies go first; C06 waits while 0x2c is 0
 * and the self check bit (0x30 bit 7) is up, or 3DX+ JP latches E20. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "strpcb.h"
#include "../log/log.h"

#define FRAME_LENGTH 10
#define FRAME_HEADER 0xFF
#define LINUX_FIONREAD 0x541B

static const uint8_t REPLY_HEALTHY[3] = {'E', '0', '0'};
static const uint8_t REPLY_MOTOR_ON[3] = {'C', '0', '1'};
static const uint8_t REPLY_MOTOR_OFF[3] = {'C', '0', '6'}; /* also "self check done" */

static CRITICAL_SECTION g_lock;
static bool g_ready;

static uint8_t g_command[64];
static size_t g_commandLength;

static uint8_t g_ack[3];
static size_t g_ackLength;

static uint8_t g_volunteered[6];
static size_t g_volunteeredLength;

static uint8_t *const *g_kickback; /* &clKickback::sm_instance */
static StrpcbFrameSink g_frameSink;
static bool g_motorRunning;
static unsigned long g_frames;

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

static StrpcbPositionSource g_position;

static bool position_requested(void)
{
    const uint8_t *board = g_kickback ? *g_kickback : NULL;
    return board && board[0x71] != 0;
}

static int board_state(void)
{
    const uint8_t *board = g_kickback ? *g_kickback : NULL;
    int state = -1;
    if (board)
        memcpy(&state, board + 0x2c, sizeof(state));
    return state;
}

static bool volunteered_welcome(void)
{
    if (g_volunteeredLength < 3 || memcmp(g_volunteered, REPLY_MOTOR_OFF, 3) != 0)
        return true;
    return !(board_state() == 0 && ((*g_kickback)[0x30] & 0x80) != 0);
}

static void consume_frames(void)
{
    for (;;)
    {
        size_t skip = 0;
        while (skip < g_commandLength && g_command[skip] != FRAME_HEADER)
            ++skip;
        if (skip)
        {
            memmove(g_command, g_command + skip, g_commandLength - skip);
            g_commandLength -= skip;
        }
        if (g_commandLength < FRAME_LENGTH)
            return;

        if (g_frames++ == 0)
            log_debug(plfFFB, "STRPCB: first frame from the game");
        if (g_frameSink)
            g_frameSink(g_command);
        memmove(g_command, g_command + FRAME_LENGTH, g_commandLength - FRAME_LENGTH);
        g_commandLength -= FRAME_LENGTH;

        /* I/F INITIALIZE (0x71) waits for 'H' and the 10-bit wheel position. */
        if (position_requested() && g_position)
        {
            uint16_t position = g_position();
            g_ack[0] = 'H';
            g_ack[1] = (uint8_t)(position >> 8);
            g_ack[2] = (uint8_t)position;
        }
        else
            memcpy(g_ack, REPLY_HEALTHY, 3);
        g_ackLength = 3;
    }
}

void strpcb_set_position_source(StrpcbPositionSource source)
{
    lock();
    g_position = source;
    unlock();
}

void strpcb_set_frame_sink(StrpcbFrameSink sink)
{
    lock();
    g_frameSink = sink;
    unlock();
}

void strpcb_set_instance(void *const *instance)
{
    lock();
    g_kickback = (uint8_t *const *)instance;
    unlock();
}

void strpcb_report_motor_power(bool running)
{
    lock();
    bool changed = g_motorRunning != running;
    g_motorRunning = running;
    /* Every call: waitOffPower reads its C06 once. */
    memcpy(g_volunteered, running ? REPLY_MOTOR_ON : REPLY_MOTOR_OFF, 3);
    g_volunteeredLength = 3;
    unlock();

    if (changed)
        log_debug(plfFFB, "STRPCB: motor %s", running ? "running (C01)" : "stopped (C06)");
}

void strpcb_report_self_check(void)
{
    lock();
    memcpy(g_volunteered, REPLY_HEALTHY, 3);
    memcpy(g_volunteered + 3, REPLY_MOTOR_OFF, 3);
    g_volunteeredLength = 6;
    int state = board_state();
    unlock();

    log_debug(plfFFB, "STRPCB: self check requested, answering E00 then C06 (state %d)", state);
}

static int bytes_available(void)
{
    if (g_volunteeredLength && volunteered_welcome())
        return 3;
    return (int)g_ackLength;
}

int strpcbInit(void)
{
    log_info(plfFFB, "STRPCB: board attached on /dev/ttyM1");
    lock();
    g_commandLength = 0;
    g_ackLength = 0;
    g_volunteeredLength = 0;
    unlock();
    return 0;
}

ssize_t strpcbRead(int fd, void *buf, size_t count)
{
    static unsigned long reads;
    if (reads++ == 0)
        log_debug(plfFFB, "STRPCB: first read from the game (fd %d)", fd);
    lock();

    /* The game decodes exactly three bytes per read. */
    uint8_t *source = NULL;
    size_t *length = NULL;
    if (g_volunteeredLength && volunteered_welcome())
    {
        source = g_volunteered;
        length = &g_volunteeredLength;
    }
    else if (g_ackLength)
    {
        source = g_ack;
        length = &g_ackLength;
    }

    if (!source)
    {
        unlock();
        errno = EAGAIN;
        return -1;
    }

    size_t taken = count < 3 ? count : 3;
    if (taken > *length)
        taken = *length;
    memcpy(buf, source, taken);
    memmove(source, source + taken, *length - taken);
    *length -= taken;
    unlock();
    /* The game latches any unexpected errno. */
    errno = 0;
    return (ssize_t)taken;
}

ssize_t strpcbWrite(int fd, const void *buf, size_t count)
{
    (void)fd;
    lock();
    const uint8_t *in = (const uint8_t *)buf;
    for (size_t i = 0; i < count; ++i)
    {
        if (g_commandLength == sizeof(g_command))
            g_commandLength = 0;
        g_command[g_commandLength++] = in[i];
    }
    consume_frames();
    unlock();
    /* The game latches any unexpected errno. */
    errno = 0;
    return (ssize_t)count;
}

int strpcbIoctl(int fd, unsigned long request, void *data)
{
    (void)fd;
    if (request == LINUX_FIONREAD && data)
    {
        lock();
        *(int *)data = bytes_available();
        unlock();
    }
    return 0;
}

int strpcbClose(int fd)
{
    (void)fd;
    lock();
    log_debug(plfFFB, "STRPCB: port closed (state %d)", board_state());
    g_commandLength = 0;
    g_ackLength = 0;
    g_volunteeredLength = 0;
    unlock();
    return 0;
}
