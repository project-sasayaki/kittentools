#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "namcoJvio.h"
#include "jvs.h"
#include "../log/log.h"

#define LINUX_FIONREAD 0x541B
#define BUFFER_SIZE 4096

static CRITICAL_SECTION g_lock;
static bool g_ready;

static uint8_t g_request[BUFFER_SIZE];
static size_t g_requestLength;
static uint8_t g_reply[BUFFER_SIZE];
static size_t g_replyLength;

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

static size_t take_frame(uint8_t *decoded, size_t *decodedLength)
{
    *decodedLength = 0;
    if (g_requestLength == 0)
        return 0;
    if (g_request[0] != SYNC)
        return 1;

    size_t raw = 1;
    size_t have = 0;
    size_t needed = 2;
    while (have < needed)
    {
        if (raw >= g_requestLength)
            return 0;

        uint8_t value = g_request[raw];
        if (value == SYNC)
            return raw;
        if (value == ESCAPE)
        {
            if (raw + 1 >= g_requestLength)
                return 0;
            value = (uint8_t)(g_request[raw + 1] + 1);
            raw += 2;
        }
        else
        {
            raw += 1;
        }

        decoded[have++] = value;
        if (have == 2)
            needed = 2 + (size_t)value;
    }

    *decodedLength = have;
    return raw;
}

static void hex(char *out, size_t outSize, const uint8_t *bytes, size_t count)
{
    size_t at = 0;
    out[0] = '\0';
    for (size_t i = 0; i < count && at + 4 < outSize; i++)
        at += (size_t)snprintf(out + at, outSize - at, "%02X ", bytes[i]);
}

static void answer(const uint8_t *packet, size_t length)
{
    static unsigned long exchanges;
    bool trace = exchanges < 16 || exchanges % 600 == 0;
    exchanges++;

    int replySize = 0;
    JVSStatus status = JvsHandleUnescaped(packet, length, &replySize);
    if (trace)
    {
        char in[3 * 64 + 4], out[3 * 64 + 4];
        hex(in, sizeof(in), packet, length);
        hex(out, sizeof(out), getOutputBuffer(), status == JVS_STATUS_SUCCESS ? (size_t)replySize : 0);
        log_debug(plfJvs, "JVIO %lu: %s-> %s", exchanges, in, out);
    }
    if (status != JVS_STATUS_SUCCESS || replySize <= 0)
        return;

    /* A reset is a broadcast: no board answers it. */
    if (length > 2 && packet[2] == CMD_RESET)
        return;

    if (g_replyLength + (size_t)replySize > sizeof(g_reply))
        g_replyLength = 0;
    memcpy(g_reply + g_replyLength, getOutputBuffer(), (size_t)replySize);
    g_replyLength += (size_t)replySize;
}

static void consume_requests(void)
{
    uint8_t decoded[JVS_MAX_PACKET_SIZE + 2];
    for (;;)
    {
        size_t decodedLength = 0;
        size_t raw = take_frame(decoded, &decodedLength);
        if (raw == 0)
            return;
        memmove(g_request, g_request + raw, g_requestLength - raw);
        g_requestLength -= raw;
        if (decodedLength)
            answer(decoded, decodedLength);
    }
}

int namcoJvioInit(void)
{
    lock();
    g_requestLength = 0;
    g_replyLength = 0;
    unlock();
    log_info(plfJvs, "JVIO: FCA-1 board attached on /dev/ttyM3");
    return 0;
}

ssize_t namcoJvioRead(int fd, void *buf, size_t count)
{
    (void)fd;
    lock();
    if (g_replyLength == 0)
    {
        unlock();
        errno = EAGAIN;
        return -1;
    }
    size_t taken = count < g_replyLength ? count : g_replyLength;
    memcpy(buf, g_reply, taken);
    memmove(g_reply, g_reply + taken, g_replyLength - taken);
    g_replyLength -= taken;
    unlock();
    /* The game latches any unexpected errno. */
    errno = 0;
    return (ssize_t)taken;
}

ssize_t namcoJvioWrite(int fd, const void *buf, size_t count)
{
    (void)fd;
    static bool first = true;
    if (first)
    {
        first = false;
        log_debug(plfJvs, "JVIO: first request from the game's JVS master");
    }

    lock();
    if (g_requestLength + count > sizeof(g_request))
        g_requestLength = 0;
    if (count <= sizeof(g_request))
    {
        memcpy(g_request + g_requestLength, buf, count);
        g_requestLength += count;
    }
    consume_requests();
    unlock();
    /* The game latches any unexpected errno. */
    errno = 0;
    return (ssize_t)count;
}

int namcoJvioIoctl(int fd, unsigned long request, void *data)
{
    (void)fd;
    if (request == LINUX_FIONREAD && data)
    {
        lock();
        *(int *)data = (int)g_replyLength;
        unlock();
    }
    return 0;
}

int namcoJvioClose(int fd)
{
    (void)fd;
    lock();
    g_requestLength = 0;
    g_replyLength = 0;
    unlock();
    return 0;
}
