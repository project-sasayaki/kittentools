#include "elf32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool load_elf32(ElfFile *elf, const char *path)
{
    memset(elf, 0, sizeof(*elf));

    FILE *f = fopen(path, "rb");
    if (!f)
        return false;

    /* Read entire file into memory */
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return false;
    }
    long fsize = ftell(f);
    if (fsize <= 0)
    {
        fclose(f);
        return false;
    }
    rewind(f);

    elf->rawData = (char *)malloc((size_t)fsize);
    if (!elf->rawData)
    {
        fclose(f);
        return false;
    }

    if (fread(elf->rawData, 1, (size_t)fsize, f) != (size_t)fsize)
    {
        free(elf->rawData);
        elf->rawData = NULL;
        fclose(f);
        return false;
    }
    fclose(f);
    elf->rawSize = (size_t)fsize;


    if (elf->rawSize < sizeof(Elf32_Ehdr))
        goto fail;
    if ((unsigned char)elf->rawData[EI_MAG0] != ELFMAG0 ||
        (unsigned char)elf->rawData[EI_MAG1] != ELFMAG1 ||
        (unsigned char)elf->rawData[EI_MAG2] != ELFMAG2 ||
        (unsigned char)elf->rawData[EI_MAG3] != ELFMAG3)
        goto fail;

    memcpy(&elf->ehdr, elf->rawData, sizeof(Elf32_Ehdr));

    if (elf->ehdr.e_ident[EI_CLASS] != ELFCLASS32)
        goto fail;

    if (elf->ehdr.e_phnum > 0 && elf->ehdr.e_phentsize >= sizeof(Elf32_Phdr))
    {
        size_t bytes = (size_t)elf->ehdr.e_phnum * sizeof(Elf32_Phdr);
        elf->phdrs = (Elf32_Phdr *)malloc(bytes);
        if (!elf->phdrs)
            goto fail;
        for (Elf32_Half i = 0; i < elf->ehdr.e_phnum; ++i)
        {
            const char *src = elf->rawData + elf->ehdr.e_phoff +
                              (size_t)i * elf->ehdr.e_phentsize;
            memcpy(&elf->phdrs[i], src, sizeof(Elf32_Phdr));
        }
    }

    if (elf->ehdr.e_shnum > 0 && elf->ehdr.e_shentsize >= sizeof(Elf32_Shdr))
    {
        size_t bytes = (size_t)elf->ehdr.e_shnum * sizeof(Elf32_Shdr);
        elf->shdrs = (Elf32_Shdr *)malloc(bytes);
        if (!elf->shdrs)
            goto fail;
        for (Elf32_Half i = 0; i < elf->ehdr.e_shnum; ++i)
        {
            const char *src = elf->rawData + elf->ehdr.e_shoff +
                              (size_t)i * elf->ehdr.e_shentsize;
            memcpy(&elf->shdrs[i], src, sizeof(Elf32_Shdr));
        }

        if (elf->ehdr.e_shstrndx != 0 && elf->ehdr.e_shstrndx < elf->ehdr.e_shnum)
        {
            Elf32_Shdr *strSec = &elf->shdrs[elf->ehdr.e_shstrndx];
            if (strSec->sh_offset + strSec->sh_size <= elf->rawSize)
            {
                elf->shstrtab     = elf->rawData + strSec->sh_offset;
                elf->shstrtabSize = strSec->sh_size;
            }
        }
    }

    return true;

fail:
    free_elf32(elf);
    return false;
}

void free_elf32(ElfFile *elf)
{
    free(elf->phdrs);
    free(elf->shdrs);
    free(elf->rawData);
    memset(elf, 0, sizeof(*elf));
}

uint16_t get_class(const ElfFile *elf)   { return elf->ehdr.e_ident[EI_CLASS]; }
uint16_t get_machine(const ElfFile *elf) { return elf->ehdr.e_machine; }
uint16_t get_type(const ElfFile *elf)    { return elf->ehdr.e_type; }
uint32_t get_entry(const ElfFile *elf)   { return elf->ehdr.e_entry; }

size_t get_segment_count(const ElfFile *elf)
{
    return elf->ehdr.e_phnum;
}

const Elf32_Phdr *get_segment(const ElfFile *elf, size_t i)
{
    if (i >= elf->ehdr.e_phnum)
        return NULL;
    return &elf->phdrs[i];
}

size_t get_section_count(const ElfFile *elf)
{
    return elf->ehdr.e_shnum;
}

const Elf32_Shdr *get_section(const ElfFile *elf, size_t i)
{
    if (i >= elf->ehdr.e_shnum)
        return NULL;
    return &elf->shdrs[i];
}

const char *get_section_name(const ElfFile *elf, size_t i)
{
    if (i >= elf->ehdr.e_shnum || !elf->shstrtab)
        return "";
    Elf32_Word nameOff = elf->shdrs[i].sh_name;
    if (nameOff >= elf->shstrtabSize)
        return "";
    return elf->shstrtab + nameOff;
}

const char *get_section_data(const ElfFile *elf, size_t i)
{
    if (i >= elf->ehdr.e_shnum)
        return NULL;
    const Elf32_Shdr *sh = &elf->shdrs[i];
    if (sh->sh_type == SHT_NOBITS)
        return NULL;
    if (sh->sh_offset + sh->sh_size > elf->rawSize)
        return NULL;
    return elf->rawData + sh->sh_offset;
}

size_t find_section_by_type(const ElfFile *elf, Elf32_Word type)
{
    for (size_t i = 0; i < elf->ehdr.e_shnum; ++i)
    {
        if (elf->shdrs[i].sh_type == type)
            return i;
    }
    return (size_t)-1;
}

const Elf32_Rel *get_rel_array(const ElfFile *elf, const Elf32_Shdr *sec, size_t *outCount)
{
    if (outCount)
        *outCount = 0;
    if (!sec || sec->sh_type != SHT_REL || sec->sh_entsize == 0)
        return NULL;
    if (sec->sh_offset + sec->sh_size > elf->rawSize)
        return NULL;
    if (outCount)
        *outCount = sec->sh_size / sec->sh_entsize;
    return (const Elf32_Rel *)(elf->rawData + sec->sh_offset);
}

const Elf32_Rela *get_rela_array(const ElfFile *elf, const Elf32_Shdr *sec, size_t *outCount)
{
    if (outCount)
        *outCount = 0;
    if (!sec || sec->sh_type != SHT_RELA || sec->sh_entsize == 0)
        return NULL;
    if (sec->sh_offset + sec->sh_size > elf->rawSize)
        return NULL;
    if (outCount)
        *outCount = sec->sh_size / sec->sh_entsize;
    return (const Elf32_Rela *)(elf->rawData + sec->sh_offset);
}

const Elf32_Sym *get_symbol_array(const ElfFile *elf, const Elf32_Shdr *sec,
                                  size_t *outCount, const char **outStrTab, size_t *outStrTabSize)
{
    if (outCount)
        *outCount = 0;
    if (outStrTab)
        *outStrTab = NULL;
    if (outStrTabSize)
        *outStrTabSize = 0;
    if (!sec || (sec->sh_type != SHT_SYMTAB && sec->sh_type != SHT_DYNSYM) || sec->sh_entsize == 0)
        return NULL;
    if (sec->sh_offset + sec->sh_size > elf->rawSize)
        return NULL;

    if (outCount)
        *outCount = sec->sh_size / sec->sh_entsize;

    /* Resolve the linked string table once for the caller. */
    if (outStrTab && sec->sh_link < elf->ehdr.e_shnum)
    {
        const Elf32_Shdr *strSec = &elf->shdrs[sec->sh_link];
        if (strSec->sh_offset + strSec->sh_size <= elf->rawSize)
        {
            *outStrTab = elf->rawData + strSec->sh_offset;
            if (outStrTabSize)
                *outStrTabSize = strSec->sh_size;
        }
    }
    return (const Elf32_Sym *)(elf->rawData + sec->sh_offset);
}

const Elf32_Dyn *get_dynamic_array(const ElfFile *elf, const Elf32_Shdr *sec, size_t *outCount)
{
    if (outCount)
        *outCount = 0;
    if (!sec || sec->sh_type != SHT_DYNAMIC || sec->sh_entsize == 0)
        return NULL;
    if (sec->sh_offset + sec->sh_size > elf->rawSize)
        return NULL;
    if (outCount)
        *outCount = sec->sh_size / sec->sh_entsize;
    return (const Elf32_Dyn *)(elf->rawData + sec->sh_offset);
}
