#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "elf32.h"


typedef struct ElfLoader
{
    ElfFile   elf;
    bool      relocated;
    bool      initialized;
    char      path[512];

    void     *baseAddress;
    void     *imageBase;
    uint32_t  loadBias;
} ElfLoader;


ElfLoader *create(void);
void       destroy(ElfLoader *loader);
bool execute(ElfLoader *loader, int argc, char **argv, char **envp);
void pre_reserve_address_space(const char *path);
bool load(ElfLoader *loader, const char *path);
void *get_base_address(const ElfLoader *loader);
int hook_ret_zero(void);