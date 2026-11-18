#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "serialLink.h"
#include "../elfLoader/termios.h"
#include "../log/log.h"

#define LINUX_FIONREAD 0x541B

#define L_CBAUD   0x0000100F
#define L_CSIZE   0x00000030
#define L_CSTOPB  0x00000040
#define L_PARENB  0x00000100
#define L_PARODD  0x00000200
#define L_CMSPAR  0x40000000
#define L_CRTSCTS 0x80000000
#define L_IXON    0x00000400
#define L_IXOFF   0x00001000
#define L_INLCR   0x00000040
#define L_IGNCR   0x00000080
#define L_ICRNL   0x00000100
#define L_OPOST   0x00000001
#define L_ICANON  0x00000002
#define L_ECHO    0x00000008
#define L_VSTART  8
#define L_VSTOP   9

#define QUEUE_SIZE 4096
#define RETRY_MS 1000
#define COM_READ_WAIT_MS 1000

static CRITICAL_SECTION g_lock;
static HANDLE g_kick;
static char g_path[MAX_PATH];
static const char *g_name = "serial link";
static bool g_pipe;

static uint8_t g_rx[QUEUE_SIZE];
static size_t g_rxLength;
static uint8_t g_tx[QUEUE_SIZE];
static size_t g_txLength;
static bool g_open;
static struct linux_termios g_termios;
static bool g_haveTermios, g_termiosChanged;

static void lock(void) { EnterCriticalSection(&g_lock); }
static void unlock(void) { LeaveCriticalSection(&g_lock); }

static unsigned baud_of(unsigned code)
{
    static const unsigned low[16] = {0, 50, 75, 110, 134, 150, 200, 300, 600, 1200,
                                     1800, 2400, 4800, 9600, 19200, 38400};
    static const unsigned high[] = {0, 57600, 115200, 230400, 460800, 500000, 576000,
                                    921600, 1000000, 1152000, 1500000, 2000000};
    if (code < 16)
        return low[code];
    if ((code & ~0xFu) == 0x1000 && (code & 0xF) < sizeof(high) / sizeof(high[0]))
        return high[code & 0xF];
    return 0;
}

/* glibc's cfsetospeed keeps the B constant in c_ospeed (and in CBAUD). */
static bool termios_to_dcb(const struct linux_termios *t, DCB *dcb)
{
    unsigned baud = baud_of(t->c_ospeed);
    if (!baud)
        baud = baud_of(t->c_cflag & L_CBAUD);
    if (!baud)
        return false;

    dcb->BaudRate = baud;
    dcb->ByteSize = (BYTE)(5 + ((t->c_cflag & L_CSIZE) >> 4));
    dcb->StopBits = (t->c_cflag & L_CSTOPB) ? TWOSTOPBITS : ONESTOPBIT;
    dcb->fParity = (t->c_cflag & L_PARENB) != 0;
    if (!(t->c_cflag & L_PARENB))
        dcb->Parity = NOPARITY;
    else if (t->c_cflag & L_CMSPAR)
        dcb->Parity = (t->c_cflag & L_PARODD) ? MARKPARITY : SPACEPARITY;
    else
        dcb->Parity = (t->c_cflag & L_PARODD) ? ODDPARITY : EVENPARITY;

    bool rtscts = (t->c_cflag & L_CRTSCTS) != 0;
    dcb->fOutxCtsFlow = rtscts;
    dcb->fRtsControl = rtscts ? RTS_CONTROL_HANDSHAKE : RTS_CONTROL_ENABLE;
    dcb->fDtrControl = DTR_CONTROL_ENABLE;
    dcb->fOutxDsrFlow = FALSE;
    dcb->fDsrSensitivity = FALSE;
    dcb->fOutX = (t->c_iflag & L_IXON) != 0;
    dcb->fInX = (t->c_iflag & L_IXOFF) != 0;
    dcb->XonChar = (char)t->c_cc[L_VSTART];
    dcb->XoffChar = (char)t->c_cc[L_VSTOP];
    dcb->fBinary = TRUE;
    dcb->fNull = FALSE;
    dcb->fErrorChar = FALSE;
    dcb->fAbortOnError = FALSE;
    return true;
}

static void warn_cooked(const struct linux_termios *t)
{
    if ((t->c_lflag & (L_ICANON | L_ECHO)) || (t->c_iflag & (L_INLCR | L_IGNCR | L_ICRNL)) ||
        (t->c_oflag & L_OPOST))
        log_warn(plfMisc, "%s: the game asks for tty translations (iflag 0x%x oflag 0x%x lflag 0x%x); "
                 "bytes pass unchanged", g_name, t->c_iflag, t->c_oflag, t->c_lflag);
}

static void apply_termios(HANDLE handle, const struct linux_termios *t)
{
    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle, &dcb))
    {
        log_warn(plfMisc, "%s: GetCommState failed (%lu)", g_name, GetLastError());
        return;
    }
    if (!termios_to_dcb(t, &dcb))
    {
        log_warn(plfMisc, "%s: unknown speed 0x%x; the port keeps its setting", g_name, t->c_ospeed);
        return;
    }
    warn_cooked(t);
    if (!SetCommState(handle, &dcb))
    {
        log_warn(plfMisc, "%s: SetCommState %lu baud failed (%lu)", g_name, dcb.BaudRate, GetLastError());
        return;
    }
    static const char parity[] = "NOEMS";
    log_info(plfMisc, "%s: %lu %d%c%s%s", g_name, dcb.BaudRate, dcb.ByteSize, parity[dcb.Parity],
             dcb.StopBits == TWOSTOPBITS ? "2" : "1", dcb.fOutxCtsFlow ? " RTS/CTS" : "");
}

static HANDLE connect_far_end(void)
{
    char device[MAX_PATH + 8];
    if (g_pipe || strncmp(g_path, "\\\\.\\", 4) == 0)
        snprintf(device, sizeof(device), "%s", g_path);
    else
        snprintf(device, sizeof(device), "\\\\.\\%s", g_path);

    HANDLE handle = CreateFileA(device, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                FILE_FLAG_OVERLAPPED, NULL);
    if (handle == INVALID_HANDLE_VALUE || g_pipe)
        return handle;

    SetupComm(handle, QUEUE_SIZE, QUEUE_SIZE);
    /* A read completes on the first byte, or empty after the wait. */
    COMMTIMEOUTS timeouts = {MAXDWORD, MAXDWORD, COM_READ_WAIT_MS, 0, 0};
    SetCommTimeouts(handle, &timeouts);

    /* 9600 8N1 until the game sets the line. */
    struct linux_termios t;
    memset(&t, 0, sizeof(t));
    t.c_cflag = 0x0D | 0x30 | 0x80;
    t.c_ospeed = t.c_ispeed = 0x0D;
    lock();
    if (g_haveTermios)
        t = g_termios;
    g_termiosChanged = false;
    unlock();
    apply_termios(handle, &t);
    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return handle;
}

static DWORD WINAPI link_thread(void *unused)
{
    (void)unused;
    HANDLE handle = INVALID_HANDLE_VALUE;
    OVERLAPPED readOv, writeOv;
    memset(&readOv, 0, sizeof(readOv));
    memset(&writeOv, 0, sizeof(writeOv));
    readOv.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    writeOv.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    bool reading = false, writing = false;
    uint8_t readBuffer[512], writeBuffer[512];
    DWORD writeLength = 0, writeDone = 0;
    ULONGLONG nextTry = 0;
    bool reportedAbsent = false;

    for (;;)
    {
        lock();
        bool wanted = g_open;
        bool changed = g_termiosChanged;
        struct linux_termios t = g_termios;
        g_termiosChanged = false;
        if (handle == INVALID_HANDLE_VALUE)
            g_txLength = 0;     /* no far end to receive it */
        unlock();

        bool broken = false;
        if (handle == INVALID_HANDLE_VALUE && wanted && GetTickCount64() >= nextTry)
        {
            handle = connect_far_end();
            if (handle == INVALID_HANDLE_VALUE)
            {
                if (!reportedAbsent)
                    log_warn(plfMisc, "%s: cannot open %s (%lu); retrying every %d ms", g_name, g_path,
                             GetLastError(), RETRY_MS);
                reportedAbsent = true;
                nextTry = GetTickCount64() + RETRY_MS;
            }
            else
            {
                log_info(plfMisc, "%s: connected to %s", g_name, g_path);
                reportedAbsent = false;
                changed = false;
            }
        }

        if (handle != INVALID_HANDLE_VALUE && !wanted)
            broken = true;
        else if (handle != INVALID_HANDLE_VALUE)
        {
            if (changed && !g_pipe)
                apply_termios(handle, &t);

            if (!reading)
            {
                ResetEvent(readOv.hEvent);
                if (ReadFile(handle, readBuffer, sizeof(readBuffer), NULL, &readOv) ||
                    GetLastError() == ERROR_IO_PENDING)
                    reading = true;
                else
                    broken = true;
            }
            if (!writing && !broken)
            {
                if (writeDone >= writeLength)
                {
                    lock();
                    writeLength = (DWORD)(g_txLength < sizeof(writeBuffer) ? g_txLength : sizeof(writeBuffer));
                    memcpy(writeBuffer, g_tx, writeLength);
                    memmove(g_tx, g_tx + writeLength, g_txLength - writeLength);
                    g_txLength -= writeLength;
                    unlock();
                    writeDone = 0;
                }
                if (writeDone < writeLength)
                {
                    ResetEvent(writeOv.hEvent);
                    if (WriteFile(handle, writeBuffer + writeDone, writeLength - writeDone, NULL, &writeOv) ||
                        GetLastError() == ERROR_IO_PENDING)
                        writing = true;
                    else
                        broken = true;
                }
            }
        }

        if (!broken)
        {
            HANDLE waits[3] = {g_kick};
            DWORD count = 1;
            if (reading)
                waits[count++] = readOv.hEvent;
            if (writing)
                waits[count++] = writeOv.hEvent;
            DWORD timeout = INFINITE;
            if (handle == INVALID_HANDLE_VALUE && wanted)
            {
                ULONGLONG now = GetTickCount64();
                timeout = nextTry > now ? (DWORD)(nextTry - now) : 0;
            }
            WaitForMultipleObjects(count, waits, FALSE, timeout);
        }

        DWORD moved;
        if (!broken && reading && WaitForSingleObject(readOv.hEvent, 0) == WAIT_OBJECT_0)
        {
            reading = false;
            if (!GetOverlappedResult(handle, &readOv, &moved, FALSE) && GetLastError() != ERROR_MORE_DATA)
                broken = true;
            else if (moved)
            {
                lock();
                size_t room = sizeof(g_rx) - g_rxLength;
                size_t kept = moved < room ? moved : room;
                memcpy(g_rx + g_rxLength, readBuffer, kept);
                g_rxLength += kept;
                unlock();
            }
        }
        if (!broken && writing && WaitForSingleObject(writeOv.hEvent, 0) == WAIT_OBJECT_0)
        {
            writing = false;
            if (!GetOverlappedResult(handle, &writeOv, &moved, FALSE))
                broken = true;
            else
                writeDone += moved;
        }

        if (broken)
        {
            if (wanted)
                log_warn(plfMisc, "%s: %s is gone (%lu); reconnecting", g_name, g_path, GetLastError());
            CancelIo(handle);
            if (reading)
                GetOverlappedResult(handle, &readOv, &moved, TRUE);
            if (writing)
                GetOverlappedResult(handle, &writeOv, &moved, TRUE);
            reading = writing = false;
            writeLength = writeDone = 0;
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
            nextTry = wanted ? GetTickCount64() + RETRY_MS : 0;
        }
    }
    return 0;
}

void serialLinkConfigure(const char *path, const char *name)
{
    snprintf(g_path, sizeof(g_path), "%s", path ? path : "");
    if (name)
        g_name = name;
    g_pipe = _strnicmp(g_path, "\\\\.\\pipe\\", 9) == 0;
}

int serialLinkInit(void)
{
    InitializeCriticalSection(&g_lock);
    g_kick = CreateEvent(NULL, FALSE, FALSE, NULL);
    HANDLE thread = g_kick ? CreateThread(NULL, 0, link_thread, NULL, 0, NULL) : NULL;
    if (!thread)
    {
        log_error(plfMisc, "%s: could not start the I/O thread", g_name);
        return -1;
    }
    CloseHandle(thread);
    log_info(plfMisc, "%s: %s %s", g_name, g_pipe ? "named pipe" : "serial port", g_path);
    return 0;
}

/* Any call means the game has opened the line. */
static void opened(void)
{
    if (!g_open)
    {
        g_open = true;
        SetEvent(g_kick);
    }
}

ssize_t serialLinkRead(int fd, void *buf, size_t count)
{
    (void)fd;
    lock();
    opened();
    if (g_rxLength == 0)
    {
        unlock();
        errno = EAGAIN;
        return -1;
    }
    size_t taken = count < g_rxLength ? count : g_rxLength;
    memcpy(buf, g_rx, taken);
    memmove(g_rx, g_rx + taken, g_rxLength - taken);
    g_rxLength -= taken;
    unlock();
    /* The game latches any unexpected errno. */
    errno = 0;
    return (ssize_t)taken;
}

ssize_t serialLinkWrite(int fd, const void *buf, size_t count)
{
    (void)fd;
    lock();
    opened();
    size_t room = sizeof(g_tx) - g_txLength;
    if (room == 0 && count)
    {
        unlock();
        errno = EAGAIN;
        return -1;
    }
    size_t taken = count < room ? count : room;
    memcpy(g_tx + g_txLength, buf, taken);
    g_txLength += taken;
    unlock();
    SetEvent(g_kick);
    errno = 0;
    return (ssize_t)taken;
}

int serialLinkIoctl(int fd, unsigned long request, void *data)
{
    (void)fd;
    lock();
    opened();
    if (request == LINUX_FIONREAD && data)
        *(int *)data = (int)g_rxLength;
    unlock();
    return 0;
}

int serialLinkSetAttr(int fd, const struct linux_termios *termios)
{
    (void)fd;
    lock();
    opened();
    g_termios = *termios;
    g_haveTermios = true;
    g_termiosChanged = true;
    unlock();
    SetEvent(g_kick);
    return 0;
}

int serialLinkClose(int fd)
{
    (void)fd;
    lock();
    g_open = false;
    g_rxLength = g_txLength = 0;
    unlock();
    SetEvent(g_kick);
    return 0;
}
