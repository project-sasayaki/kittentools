#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wmmt3Disk.h"
#include "../../config/config.h"
#include "../../hooks/file.h"
#include "../../log/log.h"

#define BLOCK_SHIFT 16                 /* 64 KB cache granularity */
#define MAX_FILES 8192
#define MAX_HANDLES 512

typedef struct
{
    char *path;
    uint8_t *cached;                   /* one bit per 64 KB block */
    size_t blocks;
} DiskFile;

typedef struct
{
    int fd;
    FILE *stream;
    int file;
} Handle;

static CRITICAL_SECTION g_lock;
static DiskFile g_files[MAX_FILES];
static int g_fileCount;
static Handle g_handles[MAX_HANDLES];
static double g_bytesPerMs;
static double g_seekMs;

static int g_lastFile = -1;
static long long g_lastEnd = -1;

static double g_statDelayMs;
static unsigned long long g_statBytes;
static unsigned g_statSeeks;
static DWORD g_statSince;

static bool modelled(const char *path)
{
    return path && strncmp(path, "/proc/", 6) != 0 && strncmp(path, "/dev/", 5) != 0 &&
           strncmp(path, "/sys/", 5) != 0;
}

static int file_index(const char *path)
{
    for (int i = 0; i < g_fileCount; i++)
        if (strcmp(g_files[i].path, path) == 0)
            return i;
    if (g_fileCount >= MAX_FILES)
        return -1;
    g_files[g_fileCount].path = _strdup(path);
    return g_files[g_fileCount].path ? g_fileCount++ : -1;
}

static Handle *find_handle(int fd, FILE *stream)
{
    for (int i = 0; i < MAX_HANDLES; i++)
        if (g_handles[i].file >= 0 &&
            ((stream && g_handles[i].stream == stream) || (!stream && fd >= 0 && g_handles[i].fd == fd)))
            return &g_handles[i];
    return NULL;
}

static void on_opened(const char *guestPath, int fd, FILE *stream)
{
    if (!modelled(guestPath))
        return;
    EnterCriticalSection(&g_lock);
    Handle *h = find_handle(fd, stream);
    if (!h)
        for (int i = 0; i < MAX_HANDLES && !h; i++)
            if (g_handles[i].file < 0)
                h = &g_handles[i];
    if (h)
    {
        h->fd = fd;
        h->stream = stream;
        h->file = file_index(guestPath);
    }
    LeaveCriticalSection(&g_lock);
}

static void on_closed(int fd, FILE *stream)
{
    EnterCriticalSection(&g_lock);
    Handle *h = find_handle(fd, stream);
    if (h)
        h->file = -1;
    LeaveCriticalSection(&g_lock);
}

static size_t take_blocks(DiskFile *f, size_t first, size_t last)
{
    if (last >= f->blocks)
    {
        size_t blocks = last + 1;
        size_t oldBytes = (f->blocks + 7) / 8, newBytes = (blocks + 7) / 8;
        uint8_t *grown = realloc(f->cached, newBytes);
        if (!grown)
            return 0;
        memset(grown + oldBytes, 0, newBytes - oldBytes);
        f->cached = grown;
        f->blocks = blocks;
    }
    size_t fresh = 0;
    for (size_t b = first; b <= last; b++)
        if (!(f->cached[b >> 3] & (1u << (b & 7))))
        {
            f->cached[b >> 3] |= (uint8_t)(1u << (b & 7));
            fresh++;
        }
    return fresh;
}

static void wait_ms(double ms)
{
    static __thread double owed;
    owed += ms;
    if (owed >= 1.0)
    {
        DWORD whole = (DWORD)owed;
        owed -= whole;
        Sleep(whole);
    }
}

static void on_read(int fd, FILE *stream, long long offset, size_t bytes)
{
    if (bytes == 0 || offset < 0)
        return;
    double ms = 0;

    EnterCriticalSection(&g_lock);
    Handle *h = find_handle(fd, stream);
    if (h && h->file >= 0)
    {
        DiskFile *f = &g_files[h->file];
        size_t fresh = take_blocks(f, (size_t)(offset >> BLOCK_SHIFT),
                                   (size_t)((offset + (long long)bytes - 1) >> BLOCK_SHIFT));
        if (fresh)
        {
            size_t freshBytes = fresh << BLOCK_SHIFT;
            if (freshBytes > bytes)
                freshBytes = bytes;
            bool continues = g_lastFile == h->file && g_lastEnd == offset;
            ms = (continues ? 0 : g_seekMs) + (double)freshBytes / g_bytesPerMs;
            g_statSeeks += continues ? 0 : 1;
            g_statBytes += freshBytes;
            g_statDelayMs += ms;
        }
        g_lastFile = h->file;
        g_lastEnd = offset + (long long)bytes;
    }

    DWORD now = GetTickCount();
    if (now - g_statSince >= 10000)
    {
        if (g_statDelayMs >= 1.0)
            log_debug(plfWmmt3, "Disk: %.0f ms of cabinet disk time in the last %lu s (%.1f MB, %u seeks)",
                     g_statDelayMs, (unsigned long)((now - g_statSince) / 1000),
                     (double)g_statBytes / (1024.0 * 1024.0), g_statSeeks);
        g_statDelayMs = 0;
        g_statBytes = 0;
        g_statSeeks = 0;
        g_statSince = now;
    }
    LeaveCriticalSection(&g_lock);

    if (ms > 0)
        wait_ms(ms);
}

static const FileObserver g_observer = {on_opened, on_read, on_closed};

void wmmt3_install_disk_model(void)
{
    if (!Config.wmmt3.disk_speed_emulation)
        return;
    int mbps = Config.wmmt3.disk_mbps > 0 ? Config.wmmt3.disk_mbps : 30;
    int seek = Config.wmmt3.disk_seek_ms >= 0 ? Config.wmmt3.disk_seek_ms : 13;

    InitializeCriticalSection(&g_lock);
    for (int i = 0; i < MAX_HANDLES; i++)
        g_handles[i].file = -1;
    g_bytesPerMs = (double)mbps * 1024.0 * 1024.0 / 1000.0;
    g_seekMs = seek;
    g_statSince = GetTickCount();
    file_set_observer(&g_observer);
    log_info(plfWmmt3, "Disk: cabinet disk speed on (%d MB/s effective, %d ms per seek)", mbps, seek);
}
