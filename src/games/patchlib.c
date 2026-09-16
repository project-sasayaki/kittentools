#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "patchlib.h"
#include "../log/log.h"
#include <MinHook.h>

void setVariable(size_t address, size_t value)
{
    if (!address) return;
    patchMemory((void *)address, &value, sizeof(value));
}

void patchMachineCode(size_t address, char *value)
{
    if (!address) return;
    size_t size = strlen((void *)value);
    if (size % 2 != 0)
    {
        log_error(plfBoot, "Patch sting len should be even.\n");
        exit(EXIT_FAILURE);
    }

    size_t bufSize = size / 2;
    char *buf = (char *)malloc(bufSize);
    if (!buf)
    {
        log_error(plfBoot, "Failed to allocate %zu bytes for patch buffer\n", bufSize);
        return;
    }
    char tmpchr[3];
    char *p = value;
    for (size_t i = 0; i < bufSize; i++)
    {
        memcpy(tmpchr, p, 2);
        tmpchr[2] = '\0';
        buf[i] = (int)strtol(tmpchr, NULL, 16);
        p += 2;
    }

    patchMemory((void*)address, buf, bufSize);
    free(buf);
}

int patchMemory(void *address, const void *data, size_t size)
{
    DWORD oldProtect;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        log_error(plfBoot, "Cannot unprotect memory region to change variable\n");
        return -1;
    }
    memcpy(address, data, size);
    VirtualProtect(address, size, oldProtect, &oldProtect);
    return 0;
}

int stubRetZero()
{
    return 0;
}

void stubReturn()
{
    return;
}

int stubRetOne()
{
    return 1;
}

int stubRetThree()
{
    return 3;
}

int stubRetMinusOne()
{
    return -1;
}