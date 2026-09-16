#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "linuxStack.h"

#define AT_NULL   0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_ENTRY  9
#define AT_UID    11
#define AT_EUID   12
#define AT_GID    13
#define AT_EGID   14
#define AT_CLKTCK 17

uint32_t linuxStack_Setup(uint32_t size, int argc, char **argv,
                          uint32_t *outStackBase, uint32_t *outStackLimit,
                          uint32_t reserveSize, const ElfAuxvInfo *auxvInfo)
{
    size = (size + 4095) & ~4095;

    void *stack = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    uint32_t highAddr = (uint32_t)(uintptr_t)stack + size;
    uint32_t lowAddr  = (uint32_t)(uintptr_t)stack;

    if (outStackBase) *outStackBase = highAddr;
    if (outStackLimit) *outStackLimit = lowAddr;

    uint32_t esp = highAddr - reserveSize;

    uint32_t *ptr = (uint32_t *)(uintptr_t)esp;

    *(--ptr) = 0;
    *(--ptr) = AT_NULL;

    if (auxvInfo)
    {
        *(--ptr) = 100;                 ; *(--ptr) = AT_CLKTCK;
        *(--ptr) = 0;                   ; *(--ptr) = AT_EGID;
        *(--ptr) = 0;                   ; *(--ptr) = AT_GID;
        *(--ptr) = 0;                   ; *(--ptr) = AT_EUID;
        *(--ptr) = 0;                   ; *(--ptr) = AT_UID;
        *(--ptr) = auxvInfo->AtEntry;   ; *(--ptr) = AT_ENTRY;
        *(--ptr) = auxvInfo->AtPageSz;  ; *(--ptr) = AT_PAGESZ;
        *(--ptr) = auxvInfo->AtPhnum;   ; *(--ptr) = AT_PHNUM;
        *(--ptr) = auxvInfo->AtPhent;   ; *(--ptr) = AT_PHENT;
        *(--ptr) = auxvInfo->AtPhdr;    ; *(--ptr) = AT_PHDR;
    }

    *(--ptr) = 0;

    for (int i = argc; argv[i]; i++)
    {
        *(--ptr) = (uint32_t)(uintptr_t)argv[i];
    }

    *(--ptr) = 0;

    for (int i = argc - 1; i >= 0; i--)
    {
        *(--ptr) = (uint32_t)(uintptr_t)argv[i];
    }

    *(--ptr) = (uint32_t)argc;

    return (uint32_t)(uintptr_t)ptr;
}
