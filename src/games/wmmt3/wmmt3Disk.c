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

#define BLOCK_SHIFT 16                 /* files are charged and dropped in 64 KB pieces */
#define PAGE_SHIFT 12                  /* memory is counted in the kernel's 4 KB pages */
#define PAGES_PER_MB (1u << (20 - PAGE_SHIFT))
#define MAX_FILES 8192
#define MAX_HANDLES 512

/* A piece of a file the cabinet's Linux still holds in memory. */
typedef struct
{
    int file;
    uint32_t block;
    uint32_t newer, older;             /* by last read; 0 = none */
    uint16_t pages;
} CachedBlock;

typedef struct
{
    char *path;
    uint32_t *slot;                    /* per 64 KB block: its CachedBlock, 0 = not in memory */
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

static CachedBlock *g_cache;           /* entry 0 is unused */
static uint32_t g_cacheUsed = 1, g_cacheSize;
static uint32_t g_newest, g_oldest, g_spare;
static uint64_t g_cachedPages;
static bool g_limited;
static uint64_t g_capacityPages;

static int g_lastFile = -1;
static long long g_lastEnd = -1;

static double g_statDelayMs;
static unsigned long long g_statBytes;
static uint64_t g_statDroppedPages;
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

static void unlink_block(uint32_t index)
{
    CachedBlock *b = &g_cache[index];
    if (b->newer)
        g_cache[b->newer].older = b->older;
    else
        g_newest = b->older;
    if (b->older)
        g_cache[b->older].newer = b->newer;
    else
        g_oldest = b->newer;
}

static void make_newest(uint32_t index)
{
    CachedBlock *b = &g_cache[index];
    b->newer = 0;
    b->older = g_newest;
    if (g_newest)
        g_cache[g_newest].newer = index;
    else
        g_oldest = index;
    g_newest = index;
}

static void drop_oldest(void)
{
    uint32_t index = g_oldest;
    CachedBlock *b = &g_cache[index];
    unlink_block(index);
    g_files[b->file].slot[b->block] = 0;
    g_cachedPages -= b->pages;
    g_statDroppedPages += b->pages;
    b->older = g_spare;
    g_spare = index;
}

static uint32_t new_block(void)
{
    if (g_spare)
    {
        uint32_t index = g_spare;
        g_spare = g_cache[index].older;
        return index;
    }
    if (g_cacheUsed >= g_cacheSize)
    {
        uint32_t size = g_cacheSize ? g_cacheSize * 2 : 4096;
        CachedBlock *grown = realloc(g_cache, (size_t)size * sizeof(*grown));
        if (!grown)
            return 0;
        g_cache = grown;
        g_cacheSize = size;
    }
    return g_cacheUsed++;
}

static bool reserve_slots(DiskFile *f, size_t last)
{
    if (last < f->blocks)
        return true;
    size_t blocks = last + 1;
    uint32_t *grown = realloc(f->slot, blocks * sizeof(*grown));
    if (!grown)
        return false;
    memset(grown + f->blocks, 0, (blocks - f->blocks) * sizeof(*grown));
    f->slot = grown;
    f->blocks = blocks;
    return true;
}

/* Marks what a read touches as the most recently used; returns how many 64 KB blocks had to come from disk. */
static size_t take_blocks(int fileIndex, long long offset, size_t bytes)
{
    DiskFile *f = &g_files[fileIndex];
    long long end = offset + (long long)bytes;
    size_t first = (size_t)(offset >> BLOCK_SHIFT), last = (size_t)((end - 1) >> BLOCK_SHIFT);
    if (!reserve_slots(f, last))
        return 0;

    size_t fresh = 0;
    for (size_t block = first; block <= last; block++)
    {
        long long blockStart = (long long)block << BLOCK_SHIFT;
        long long reach = end - blockStart;
        if (reach > (1 << BLOCK_SHIFT))
            reach = 1 << BLOCK_SHIFT;
        uint16_t pages = (uint16_t)((reach + (1 << PAGE_SHIFT) - 1) >> PAGE_SHIFT);

        uint32_t index = f->slot[block];
        if (index)
        {
            CachedBlock *b = &g_cache[index];
            if (pages > b->pages)
            {
                g_cachedPages += pages - b->pages;
                b->pages = pages;
            }
            unlink_block(index);
            make_newest(index);
            continue;
        }

        fresh++;
        index = new_block();
        if (!index)
            continue;
        g_cache[index].file = fileIndex;
        g_cache[index].block = (uint32_t)block;
        g_cache[index].pages = pages;
        f->slot[block] = index;
        make_newest(index);
        g_cachedPages += pages;
    }

    while (g_limited && g_cachedPages > g_capacityPages && g_oldest)
        drop_oldest();
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

/* The time the cabinet's disk takes over this read, in ms; 0 when Linux still has the data in memory. */
static double charge_read(int fd, FILE *stream, long long offset, size_t bytes)
{
    if (bytes == 0 || offset < 0)
        return 0;
    double ms = 0;

    EnterCriticalSection(&g_lock);
    Handle *h = find_handle(fd, stream);
    if (h && h->file >= 0)
    {
        size_t fresh = take_blocks(h->file, offset, bytes);
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
            log_debug(plfWmmt3, "Disk: %.0f ms of cabinet disk time in the last %lu s (%.1f MB, %u seeks); "
                      "%.0f MB of files in memory, %.1f MB dropped",
                      g_statDelayMs, (unsigned long)((now - g_statSince) / 1000),
                      (double)g_statBytes / (1024.0 * 1024.0), g_statSeeks,
                      (double)g_cachedPages / PAGES_PER_MB, (double)g_statDroppedPages / PAGES_PER_MB);
        g_statDelayMs = 0;
        g_statBytes = 0;
        g_statDroppedPages = 0;
        g_statSeeks = 0;
        g_statSince = now;
    }
    LeaveCriticalSection(&g_lock);
    return ms;
}

static void on_read(int fd, FILE *stream, long long offset, size_t bytes)
{
    double ms = charge_read(fd, stream, offset, bytes);
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
    int ram = Config.wmmt3.cabinet_ram_mb, used = Config.wmmt3.cabinet_used_mb;

    InitializeCriticalSection(&g_lock);
    for (int i = 0; i < MAX_HANDLES; i++)
        g_handles[i].file = -1;
    g_bytesPerMs = (double)mbps * 1024.0 * 1024.0 / 1000.0;
    g_seekMs = seek;
    g_statSince = GetTickCount();

    int left = ram - (used > 0 ? used : 0);
    g_limited = ram > 0;
    g_capacityPages = g_limited && left > 0 ? (uint64_t)left * PAGES_PER_MB : 0;
    if (g_limited && left <= 0)
        log_warn(plfWmmt3, "Disk: cabinet_used_mb %d leaves nothing of cabinet_ram_mb %d for files; "
                 "every read comes from the disk", used, ram);

    file_set_observer(&g_observer);
    if (g_limited)
        log_info(plfWmmt3, "Disk: cabinet disk speed on (%d MB/s effective, %d ms per seek, %llu MB of memory "
                 "for files already read)", mbps, seek, (unsigned long long)(g_capacityPages / PAGES_PER_MB));
    else
        log_info(plfWmmt3, "Disk: cabinet disk speed on (%d MB/s effective, %d ms per seek, files already read "
                 "are never dropped)", mbps, seek);
}
