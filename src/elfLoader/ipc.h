#pragma once

#include <stdint.h>
#include <stddef.h>
#include <windows.h>

struct ShmInfo
{
    HANDLE hMap;
    void  *pMem;
    size_t size;
    int    key;
};

void ipcBridge_initBridges(void);
