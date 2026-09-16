#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>

#include "memory.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <errno.h>
#include <stdio.h>

#include "../log/log.h"

void *allocate_executable(size_t size, void *requestedAddr)
{
    void *ptr = NULL;
    if (requestedAddr)
    {
        ptr = VirtualAlloc(requestedAddr, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    }
    if (!ptr)
    {
        ptr = VirtualAlloc(requestedAddr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    }
    if (!ptr && requestedAddr != NULL)
    {
        uint8_t *scan = (uint8_t *)requestedAddr;
        uint8_t *end = scan + size;
        int canUse = 1;

        while (scan < end)
        {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(scan, &mbi, sizeof(mbi)))
            {
                canUse = 0;
                break;
            }

            SIZE_T regionSize = mbi.RegionSize;
            uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + regionSize;
            if (regionEnd > (uintptr_t)end)
                regionSize = end - scan;

            if (mbi.State == MEM_COMMIT)
            {
                DWORD oldProtect;
                if (!VirtualProtect(scan, regionSize, PAGE_EXECUTE_READWRITE, &oldProtect))
                {
                    log_error(plfMemMgr, "AllocateExecutable: VirtualProtect failed at %p (%zu bytes). Error: %lu",
                              scan, regionSize, GetLastError());
                    canUse = 0;
                    break;
                }
            }
            else if (mbi.State == MEM_FREE)
            {
                if (!VirtualAlloc(scan, regionSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))
                {
                    log_error(plfMemMgr, "AllocateExecutable: VirtualAlloc(FREE) failed at %p (%zu bytes). Error: %lu",
                              scan, regionSize, GetLastError());
                    canUse = 0;
                    break;
                }
            }
            else if (mbi.State == MEM_RESERVE)
            {
                if (!VirtualAlloc(scan, regionSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE))
                {
                    log_error(plfMemMgr, "AllocateExecutable: VirtualAlloc(RESERVE) failed at %p (%zu bytes). Error: %lu",
                              scan, regionSize, GetLastError());
                    canUse = 0;
                    break;
                }
            }

            scan = (uint8_t *)mbi.BaseAddress + mbi.RegionSize;
        }

        if (canUse)
        {
            log_warn(plfMemMgr, "AllocateExecutable: Using taken-over memory at %p (%zu bytes)", requestedAddr, size);
            ptr = requestedAddr;
        }
    }
    if (!ptr && requestedAddr != NULL)
    {
        DWORD err = GetLastError();
        log_error(plfMemMgr, "VirtualAlloc failed for address %p, size %zu. Error: %lu", requestedAddr, size, err);

        uint8_t *scan = (uint8_t *)requestedAddr;
        uint8_t *end = scan + size;

        log_error(plfMemMgr, "Scanning region %p to %p for conflicts...", scan, end);
        while (scan < end)
        {
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(scan, &mbi, sizeof(mbi)))
            {
                if (mbi.State != MEM_FREE)
                {
                    log_error(plfMemMgr, "Conflict found at %p - RegionSize: 0x%zx, State: 0x%lx, Type: 0x%lx",
                              mbi.BaseAddress, mbi.RegionSize, mbi.State, mbi.Type);
                    break;
                }
                scan += mbi.RegionSize;
            }
            else
            {
                log_error(plfMemMgr, "VirtualQuery failed at %p", scan);
                break;
            }
        }
    }
    return ptr;
}

void *reserve_address_space(size_t size, void *requestedAddr)
{
    void *result = VirtualAlloc(requestedAddr, size, MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (result)
    {
        log_debug(plfMemMgr, "Memory: Pre-reserved address space: %p - %p (%zu bytes)", result, (char *)result + size, size);
        return result;
    }

    if (!requestedAddr)
        return NULL;

    log_warn(plfMemMgr, "Memory: Pre-reserve failed at %p (%zu bytes). Taking over existing committed memory...",
             requestedAddr, size);

    uintptr_t scanAddr = (uintptr_t)requestedAddr;
    uintptr_t alignedMax = scanAddr + size;
    int allOk = 1;

    while (scanAddr < alignedMax)
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((void *)scanAddr, &mbi, sizeof(mbi)))
            break;

        if (mbi.State == MEM_FREE)
        {
            SIZE_T freeSize = mbi.RegionSize;
            uintptr_t freeEnd = (uintptr_t)mbi.BaseAddress + freeSize;
            if (freeEnd > alignedMax)
                freeSize = alignedMax - scanAddr;

            void *allocated = VirtualAlloc((void *)scanAddr, freeSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (allocated)
            {
                log_debug(plfMemMgr, "  Claimed free region at %p (%zu bytes)", allocated, freeSize);
            }
            else
            {
                log_error(plfMemMgr, "  Failed to claim free region at %p (%zu bytes). Error: %lu", (void *)scanAddr, freeSize, GetLastError());
                allOk = 0;
            }
        }
        else if (mbi.State == MEM_COMMIT)
        {
            char moduleName[MAX_PATH] = {0};
            if (mbi.Type == MEM_IMAGE || mbi.Type == MEM_MAPPED)
                GetMappedFileNameA(GetCurrentProcess(), (void *)scanAddr, moduleName, MAX_PATH);

            log_warn(plfMemMgr, "  Taking over committed region at %p (Size=0x%zx, Type=0x%lx) %s%s", mbi.BaseAddress, mbi.RegionSize, mbi.Type,
                     moduleName[0] ? "Module: " : "", moduleName[0] ? moduleName : "");

            DWORD oldProtect;
            SIZE_T protectSize = mbi.RegionSize;
            uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + protectSize;
            if (regionEnd > alignedMax)
                protectSize = alignedMax - scanAddr;

            if (!VirtualProtect((void *)scanAddr, protectSize, PAGE_EXECUTE_READWRITE, &oldProtect))
            {
                log_error(plfMemMgr, "  VirtualProtect failed at %p (%zu bytes). Error: %lu", (void *)scanAddr, protectSize, GetLastError());
                allOk = 0;
            }
        }
        else if (mbi.State == MEM_RESERVE)
        {
            SIZE_T commitSize = mbi.RegionSize;
            uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + commitSize;
            if (regionEnd > alignedMax)
                commitSize = alignedMax - scanAddr;

            void *committed = VirtualAlloc((void *)scanAddr, commitSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (committed)
            {
                log_debug(plfMemMgr, "  Committed reserved region at %p (%zu bytes)", committed, commitSize);
            }
            else
            {
                log_error(plfMemMgr, "  Failed to commit reserved region at %p (%zu bytes). Error: %lu", (void *)scanAddr, commitSize, GetLastError());
                allOk = 0;
            }
        }

        scanAddr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }

    return allOk ? requestedAddr : NULL;
}

void *hook_malloc(size_t size)
{
    return _aligned_malloc(size, 16);
}

struct mallinfo hook_mallinfo(void)
{
    struct mallinfo Info;
    memset(&Info, 0, sizeof(Info));

    HANDLE Heap = GetProcessHeap();
    if (!Heap)
        return Info;

    HeapLock(Heap);

    PROCESS_HEAP_ENTRY Entry;
    memset(&Entry, 0, sizeof(Entry));
    while (HeapWalk(Heap, &Entry))
    {
        if (Entry.wFlags & PROCESS_HEAP_ENTRY_BUSY)
        {
            Info.uordblks += (int)Entry.cbData;
        }
        else
        {
            Info.fordblks += (int)Entry.cbData;
        }
    }

    HeapUnlock(Heap);

    Info.arena = Info.uordblks + Info.fordblks;
    return Info;
}

void hook_free(void *ptr)
{
    _aligned_free(ptr);
}

void *hook_calloc(size_t nmemb, size_t size)
{
    if (nmemb != 0 && size > SIZE_MAX / nmemb)
    {
        return NULL;
    }
    size_t total = nmemb * size;
    void *ptr = _aligned_malloc(total, 16);
    if (ptr)
        memset(ptr, 0, total);
    return ptr;
}

void *hook_realloc(void *ptr, size_t size)
{
    return _aligned_realloc(ptr, size, 16);
}

void *hook_memalign(size_t alignment, size_t size)
{
    return _aligned_malloc(size, alignment);
}

int hook_posix_memalign(void **ptr, size_t alignment, size_t size)
{
    if (alignment % sizeof(void *) != 0 || (alignment & (alignment - 1)) != 0)
    {
        return EINVAL;
    }

    *ptr = _aligned_malloc(size, alignment);
    return *ptr ? 0 : ENOMEM;
}

char *hook_strndup(const char *src, size_t size)
{
    size_t len = strnlen(src, size);
    len = len < size ? len : size;
    char *dst = (char *)_aligned_malloc(len + 1, 16);
    if (!dst)
        return NULL;
    memcpy(dst, src, len);
    dst[len] = '\0';
    return dst;
}

char *hook_strdup(const char *s)
{
    if (!s)
        return NULL;
    size_t len = strlen(s) + 1;
    void *ptr = _aligned_malloc(len, 16);
    if (ptr)
        memcpy(ptr, s, len);
    return (char *)ptr;
}
