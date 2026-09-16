#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct
{
    uint32_t AtPhdr;   
    uint32_t AtPhent;  
    uint32_t AtPhnum;  
    uint32_t AtPageSz; 
    uint32_t AtEntry;  
} ElfAuxvInfo;

uint32_t linuxStack_Setup(uint32_t size, int argc, char **argv,
                          uint32_t *outStackBase, uint32_t *outStackLimit,
                          uint32_t reserveSize, const ElfAuxvInfo *auxvInfo);

