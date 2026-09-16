#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "ipc.h"
#include "collections.h"
#include "symbol.h"
#include "../log/log.h"
#include "elfLoader.h"

static IntMap g_shmMap;
int   hook_Shmget(int key, size_t size, int shmflg);
void *hook_shmat(int shmid, const void *shmaddr, int shmflg);
int   hook_shhmctl(int shmid, int cmd, void *buf);

void ipcBridge_initBridges(void)
{
    log_debug(plfBridges, "Initializing IPC Bridges...");

    intMap_Init(&g_shmMap);

    symbol_register("shmget", hook_Shmget, NULL);
    symbol_register("shmat", hook_shmat, NULL);
    symbol_register("shmctl", hook_shhmctl, NULL);
    symbol_register("shmdt", hook_ret_zero, NULL);
}

int hook_Shmget(int key, size_t size, int shmflg)
{
    if (key < 0)
    {
        log_error(plfBridges, "shmget: invalid key");
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }

    struct ShmInfo *existing = (struct ShmInfo *)intMap_Get(&g_shmMap, key);
    if (existing)
    {
        if (size > existing->size)
        {
            log_error(plfBridges, "shmget: requested size larger than existing segment");
            return -1;
        }
        return key;
    }

    char mapName[64];
    snprintf(mapName, sizeof(mapName), "zmShare%08x", (unsigned int)key);

    HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                     (DWORD)((uint64_t)size >> 32),
                                     (DWORD)(size & 0xffffffff), mapName);

    if (hMap == NULL)
    {
        DWORD err = GetLastError();
        log_error(plfBridges, "shmget: CreateFileMapping failed (%lu)", err);
        return -1;
    }

    DWORD createErr = GetLastError();

    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T actualSize = size;

    void *testView = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 1);
    if (testView)
    {
        if (VirtualQuery(testView, &mbi, sizeof(mbi)))
        {
            actualSize = mbi.RegionSize;
        }
        UnmapViewOfFile(testView);
    }

    struct ShmInfo *info = (struct ShmInfo *)calloc(1, sizeof(struct ShmInfo));
    info->hMap = hMap;
    info->pMem = NULL;
    info->size = actualSize;
    info->key = key;

    intMap_Set(&g_shmMap, key, info);

    if (createErr == ERROR_ALREADY_EXISTS)
    {
        log_trace(plfBridges, "shmget: opened existing mapping key=%d size=%zu", key, actualSize);
    }
    else
    {
        log_debug(plfBridges, "shmget: created mapping key=%d size=%zu", key, actualSize);
    }

    return key;
}

void *hook_shmat(int shmid, const void *shmaddr, int shmflg)
{
    struct ShmInfo *info = (struct ShmInfo *)intMap_Get(&g_shmMap, shmid);
    if (!info)
    {
        log_error(plfBridges, "shmat failed: Invalid shmid %d", shmid);
        return (void *)-1;
    }

    if (info->pMem)
    {
        return info->pMem;
    }

    info->pMem = MapViewOfFile(info->hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);

    if (!info->pMem)
    {
        log_error(plfBridges, "shmat failed: MapViewOfFile error %lu", GetLastError());
        return (void *)-1;
    }

    log_debug(plfBridges, "shmat: Attached id=%d at %p", shmid, info->pMem);
    return info->pMem;
}

int hook_shhmctl(int shmid, int cmd, void *buf)
{
    if (cmd == 0)
    { 
        log_debug(plfBridges, "shmctl: Removing id=%d", shmid);

        struct ShmInfo *info = (struct ShmInfo *)intMap_Get(&g_shmMap, shmid);
        if (info)
        {
            if (info->pMem)
            {
                UnmapViewOfFile(info->pMem);
            }
            if (info->hMap)
            {
                CloseHandle(info->hMap);
            }
            intMap_Remove(&g_shmMap, shmid);
            free(info);
        }
    }

    return 0;
}