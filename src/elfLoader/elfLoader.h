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

    bool      isGuestLib;
    uint32_t  initAddr;
    uint32_t  initArrayAddr;
    uint32_t  initArraySize;
    uint32_t  pltGotAddr;
} ElfLoader;


ElfLoader *create(void);
void       destroy(ElfLoader *loader);
bool execute(ElfLoader *loader, int argc, char **argv, char **envp);
void pre_reserve_address_space(const char *path);
bool load(ElfLoader *loader, const char *path);
void *get_base_address(const ElfLoader *loader);

void  elf_run_guest_initializers(void);
void *elf_lookup_guest_library_symbol(const char *name);

size_t elf_patch_writable_code_stubs(const char *prefix, void *(*resolver)(const char *name));
/* Replace an imported function in every loaded module after load; returns the implementation it replaced. */
void *elf_interpose_import(const char *name, void *replacement);
int hook_ret_zero(void);