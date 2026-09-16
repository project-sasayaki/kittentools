#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winnt.h>
#include <psapi.h>

#include "elfLoader.h"
#include "elf32.h"
#include "collections.h"
#include "memory.h"
#include "symbol.h"
#include "linuxStack.h"
#include "../log/log.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>


/* Collected in a single SHT_DYNAMIC sweep so load_dependencies() and
 * process_relocations() don't each re-scan the section table. */
typedef struct
{
    uint32_t relrAddr;
    uint32_t relrSz;
    uint32_t relAddr;
    uint32_t relSz;
    uint32_t relEntSize;
    uint32_t relaAddr;
    uint32_t relaSz;
    uint32_t relaEntSize;
    uint32_t dynsymAddr;
    uint32_t dynstrAddr;
} RelocTags;

/* DT_NEEDED entries discovered in the same sweep. The string pointers point
 * into the ELF's raw data and stay valid as long as the ElfFile is alive. */
typedef struct
{
    const char **needed;
    size_t       neededCount;
    RelocTags    reloc;
} DynInfo;

static bool map_segments_to_memory(ElfLoader *loader);
static bool export_symbols(ElfLoader *loader);
static bool resolve_vtables(ElfLoader *loader);
static bool load_dependencies(ElfLoader *loader, const DynInfo *dyn);
static bool process_relocations_with(ElfLoader *loader, const DynInfo *dyn);
static void collect_dynamic_info(ElfFile *elf, DynInfo *dyn);
static void free_dynamic_info(DynInfo *dyn);

ElfLoader *create(void)
{
    ElfLoader *loader = (ElfLoader *)calloc(1, sizeof(ElfLoader));
    if (!loader)
        return NULL;
    return loader;
}

void destroy(ElfLoader *loader)
{
    if (!loader)
        return;
    free_elf32(&loader->elf);
    free(loader);
}

void *get_base_address(const ElfLoader *loader)
{
    return loader->imageBase;
}

void pre_reserve_address_space(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return;

    Elf32_Ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, f) != 1)
    {
        fclose(f);
        return;
    }

    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 || ehdr.e_ident[EI_MAG3] != ELFMAG3)
    {
        fclose(f);
        return;
    }

    if (ehdr.e_ident[EI_CLASS] != ELFCLASS32)
    {
        fclose(f);
        return;
    }
    if (ehdr.e_type != ET_EXEC)
    {
        fclose(f);
        return;
    }

    Elf32_Phdr phdr;
    uint32_t minAddr = UINT32_MAX;
    uint32_t maxAddr = 0;

    fseek(f, ehdr.e_phoff, SEEK_SET);
    for (Elf32_Half i = 0; i < ehdr.e_phnum; ++i)
    {
        if (fread(&phdr, sizeof(phdr), 1, f) != 1)
            break;

        if (ehdr.e_phentsize > sizeof(phdr))
            fseek(f, ehdr.e_phentsize - sizeof(phdr), SEEK_CUR);

        if (phdr.p_type == PT_LOAD)
        {
            if (phdr.p_vaddr < minAddr)
                minAddr = phdr.p_vaddr;
            if (phdr.p_vaddr + phdr.p_memsz > maxAddr)
                maxAddr = phdr.p_vaddr + phdr.p_memsz;
        }
    }

    fclose(f);

    if (maxAddr <= minAddr)
        return;

    uint32_t alignedBase = minAddr & ~0xFFFF;
    uint32_t alignedMax  = (maxAddr + 0xFFFF) & ~0xFFFF;
    SIZE_T totalSize = alignedMax - alignedBase;

    reserve_address_space(totalSize, (void *)(uintptr_t)alignedBase);
}

/* ===================================================================== */
/*  Load                                                                 */
/* ===================================================================== */

bool load(ElfLoader *loader, const char *path)
{
    strncpy(loader->path, path, sizeof(loader->path) - 1);
    loader->path[sizeof(loader->path) - 1] = '\0';

    /* ParseElf */
    if (!load_elf32(&loader->elf, path))
    {
        log_error(plfELFLoader, "Failed to parse ELF file: %s", path);
        return false;
    }

    if (get_class(&loader->elf) != ELFCLASS32)
    {
        log_error(plfELFLoader, "Only 32-bit ELF files are supported");
        return false;
    }

    if (get_machine(&loader->elf) != EM_386)
    {
        log_error(plfELFLoader, "Only x86 machine type is supported");
        return false;
    }

    log_debug(plfELFLoader, "Successfully parsed 32-bit x86 ELF: %s", path);

    if (!map_segments_to_memory(loader))
        return false;

    if (!export_symbols(loader))
        return false;

    if (!resolve_vtables(loader))
        return false;

    /* Single SHT_DYNAMIC sweep feeds both dependency loading and relocation. */
    DynInfo dyn;
    collect_dynamic_info(&loader->elf, &dyn);

    bool ok = load_dependencies(loader, &dyn) && process_relocations_with(loader, &dyn);
    free_dynamic_info(&dyn);
    if (!ok)
        return false;

    return true;
}

static bool map_segments_to_memory(ElfLoader *loader)
{
    log_debug(plfELFLoader, "Mapping segments to memory...");

    ElfFile *elf = &loader->elf;
    size_t segmentsNum = get_segment_count(elf);
    if (segmentsNum == 0)
    {
        log_error(plfELFLoader, "No segments found in the ELF file");
        return false;
    }

    uint32_t minLoadAddr = UINT32_MAX;
    uint32_t maxLoadAddr = 0;

    for (size_t i = 0; i < segmentsNum; ++i)
    {
        const Elf32_Phdr *seg = get_segment(elf, i);
        if (seg->p_type == PT_LOAD)
        {
            if (seg->p_vaddr < minLoadAddr)
                minLoadAddr = seg->p_vaddr;
            if (seg->p_vaddr + seg->p_memsz > maxLoadAddr)
                maxLoadAddr = seg->p_vaddr + seg->p_memsz;
        }
    }

    if (maxLoadAddr <= minLoadAddr)
    {
        log_error(plfELFLoader, "Invalid load addresses min: 0x%x max: 0x%x", minLoadAddr, maxLoadAddr);
        return false;
    }

    size_t totalSize = maxLoadAddr - minLoadAddr;
    log_debug(plfELFLoader, "Total memory required for ELF: %zu bytes", totalSize);

    void *requestedAddress = NULL;
    uint32_t offset = 0;
    if (get_type(elf) == ET_EXEC)
    {
        uint32_t alignedAddr = minLoadAddr & ~0xFFFF;
        requestedAddress = (void *)(uintptr_t)alignedAddr;
        offset = minLoadAddr - alignedAddr;
        totalSize += offset;

        log_debug(plfELFLoader, "ET_EXEC detected. Adjusted allocation address to 64KB boundary: %p, original was 0x%x",
                  requestedAddress, (uint32_t)(uintptr_t)requestedAddress + offset);
    }

    loader->baseAddress = allocate_executable(totalSize, requestedAddress);
    if (!loader->baseAddress)
    {
        log_fatal(plfELFLoader, "Failed to allocate %zu bytes for ELF", totalSize);
        return false;
    }

    if (get_type(elf) == ET_EXEC && loader->baseAddress != requestedAddress)
    {
        log_fatal(plfELFLoader, "ET_EXEC allocation failed at required fixed address %p. Got %p instead.",
                  requestedAddress, loader->baseAddress);
        return false;
    }

    log_debug(plfELFLoader, "Allocated memory at base address: %p", loader->baseAddress);

    uint32_t actualBaseAddr = (uint32_t)(uintptr_t)loader->baseAddress;
    loader->loadBias  = actualBaseAddr + offset - minLoadAddr;
    loader->imageBase = (void *)(uintptr_t)(actualBaseAddr + offset);

    log_debug(plfELFLoader, "loadBias = 0x%x, imageBase = %p", loader->loadBias, loader->imageBase);

    for (size_t i = 0; i < segmentsNum; ++i)
    {
        const Elf32_Phdr *seg = get_segment(elf, i);
        if (seg->p_type == PT_LOAD)
        {
            uint32_t vaddr = seg->p_vaddr;
            uint32_t filesz = seg->p_filesz;
            uint32_t memsz = seg->p_memsz;

            uint8_t *dest = (uint8_t *)(uintptr_t)(vaddr + loader->loadBias);

            if (filesz > 0)
            {
                const char *segData = elf->rawData + seg->p_offset;
                memcpy(dest, segData, filesz);
            }

            if (memsz > filesz)
            {
                memset(dest + filesz, 0, memsz - filesz);
            }

            log_trace(plfELFLoader, "Mapped PT_LOAD segment to %p, size %u", dest, memsz);
        }
    }

    return true;
}

static bool load_dependencies(ElfLoader *loader, const DynInfo *dyn)
{
    log_debug(plfELFLoader, "Loading dependencies...");
    static bool librariesRegistered = false;
    if (!librariesRegistered)
    {
        librariesRegistered = true;

        symres_register_library("libCg.so", "Cg.dll");
        symres_register_library("libCgGL.so", "CgGL.dll");
        symres_register_library("libdl.so.2", NULL);
        symres_register_library("libc.so.6", NULL);
        symres_register_library("libpthread.so.0", NULL);
        symres_register_library("libgcc_s.so.1", NULL);
        symres_register_library("libsegaapi.so", NULL);
        symres_register_library("libXxf86vm.so.1", NULL);
        symres_register_library("libX11.so.6", NULL);
        symres_register_library("libXext.so.6", NULL);
        symres_register_library("libXmu.so.6", NULL);
        symres_register_library("libXi.so.6", NULL);
        symres_register_library("libGL.so.1", NULL);
        symres_register_library("libGLU.so.1", NULL);
        symres_register_library("libglut.so.3", NULL);
        symres_register_library("libkswapapi.so", NULL);
    }

    /* DT_NEEDED list was already collected by collect_dynamic_info(). */
    (void)loader;
    for (size_t j = 0; j < dyn->neededCount; ++j)
    {
        const char *str = dyn->needed[j];
        log_debug(plfELFLoader, "ELF needs library: %s", str);
        symres_load_needed_library(str);
    }
    return true;
}

static void collect_dynamic_info(ElfFile *elf, DynInfo *dyn)
{
    memset(dyn, 0, sizeof(*dyn));

    for (size_t i = 0; i < get_section_count(elf); ++i)
    {
        const Elf32_Shdr *sec = get_section(elf, i);
        if (sec->sh_type != SHT_DYNAMIC)
            continue;

        size_t dynCount = 0;
        const Elf32_Dyn *dynArr = get_dynamic_array(elf, sec, &dynCount);
        if (!dynArr)
            continue;

        /* Resolve the dynamic string table once for DT_NEEDED/DT_SONAME/etc. */
        const char *dynStr = NULL;
        size_t dynStrSize = 0;
        if (sec->sh_link < elf->ehdr.e_shnum)
        {
            const Elf32_Shdr *strSec = &elf->shdrs[sec->sh_link];
            if (strSec->sh_offset + strSec->sh_size <= elf->rawSize)
            {
                dynStr = elf->rawData + strSec->sh_offset;
                dynStrSize = strSec->sh_size;
            }
        }

        /* First sub-pass over this section: count DT_NEEDED so we can size
         * the array exactly (across all SHT_DYNAMIC sections). */
        for (size_t j = 0; j < dynCount; ++j)
        {
            if (dynArr[j].d_tag == DT_NULL)
                break;
            if (dynArr[j].d_tag == DT_NEEDED && dynStr)
                ++dyn->neededCount;
        }
    }

    dyn->needed = (const char **)malloc((dyn->neededCount ? dyn->neededCount : 1) * sizeof(const char *));

    /* Second pass: fill tags + needed[]. */
    RelocTags *r = &dyn->reloc;
    size_t neededFill = 0;
    for (size_t i = 0; i < get_section_count(elf); ++i)
    {
        const Elf32_Shdr *sec = get_section(elf, i);
        if (sec->sh_type != SHT_DYNAMIC)
            continue;

        size_t dynCount = 0;
        const Elf32_Dyn *dynArr = get_dynamic_array(elf, sec, &dynCount);
        if (!dynArr)
            continue;

        const char *dynStr = NULL;
        size_t dynStrSize = 0;
        if (sec->sh_link < elf->ehdr.e_shnum)
        {
            const Elf32_Shdr *strSec = &elf->shdrs[sec->sh_link];
            if (strSec->sh_offset + strSec->sh_size <= elf->rawSize)
            {
                dynStr = elf->rawData + strSec->sh_offset;
                dynStrSize = strSec->sh_size;
            }
        }

        for (size_t j = 0; j < dynCount; ++j)
        {
            int32_t tag = dynArr[j].d_tag;
            uint32_t value = dynArr[j].d_un.d_val;
            if (tag == DT_NULL)
                break;
            switch (tag)
            {
                case DT_RELR:      r->relrAddr = value; break;
                case DT_RELRSZ:    r->relrSz = value; break;
                case DT_REL:       r->relAddr = value; break;
                case DT_RELSZ:     r->relSz = value; break;
                case DT_RELENT:    r->relEntSize = value; break;
                case DT_RELA:      r->relaAddr = value; break;
                case DT_RELASZ:    r->relaSz = value; break;
                case DT_RELAENT:   r->relaEntSize = value; break;
                case DT_SYMTAB:    r->dynsymAddr = value; break;
                case DT_STRTAB:    r->dynstrAddr = value; break;
                case DT_NEEDED:
                    if (dynStr && value < dynStrSize && neededFill < dyn->neededCount)
                        dyn->needed[neededFill++] = dynStr + value;
                    break;
            }
        }
    }
}

static void free_dynamic_info(DynInfo *dyn)
{
    free(dyn->needed);
    dyn->needed = NULL;
    dyn->neededCount = 0;
}

#ifndef DT_RELRSZ
#define DT_RELRSZ 35
#endif
#ifndef DT_RELR
#define DT_RELR 36
#endif

static bool process_relocations_with(ElfLoader *loader, const DynInfo *dyn)
{
    if (loader->relocated)
        return true;
    loader->relocated = true;

    log_debug(plfELFLoader, "Processing relocations...");

    ElfFile *elf = &loader->elf;
    const RelocTags *r = &dyn->reloc;
    uint32_t relrAddr    = r->relrAddr;
    uint32_t relrSz      = r->relrSz;
    uint32_t relAddr     = r->relAddr;
    uint32_t relSz       = r->relSz;
    uint32_t relaAddr    = r->relaAddr;
    uint32_t relaSz      = r->relaSz;
    uint32_t dynsymAddr  = r->dynsymAddr;
    uint32_t dynstrAddr  = r->dynstrAddr;
    (void)relAddr; (void)relSz; (void)relaAddr; (void)relaSz;
    (void)dynsymAddr; (void)dynstrAddr;

    PtrMap relrPatched;
    ptrMap_Init(&relrPatched);

    log_debug(plfELFLoader, "Dynamic tags: DT_RELR=0x%x(%u), DT_REL=0x%x(%u), DT_RELA=0x%x(%u)",
              relrAddr, relrSz, relAddr, relSz, relaAddr, relaSz);

    if (relrAddr != 0 && relrSz > 0)
    {
        uint8_t *actualRelr = (uint8_t *)(uintptr_t)(relrAddr + loader->loadBias);
        uint32_t *relrData = (uint32_t *)actualRelr;
        size_t count = relrSz / sizeof(uint32_t);

        uint32_t base = 0;
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t entry = relrData[i];
            if ((entry & 1) == 0)
            {
                uint32_t *patchAddr = (uint32_t *)(uintptr_t)(entry + loader->loadBias);
                *patchAddr += loader->loadBias;
                ptrMap_Set(&relrPatched, patchAddr, patchAddr);
                base = entry + sizeof(uint32_t);
            }
            else
            {
                uint32_t bitmap = entry >> 1;
                uint32_t offset = base;
                while (bitmap != 0)
                {
                    if (bitmap & 1)
                    {
                        uint32_t *patchAddr = (uint32_t *)(uintptr_t)(offset + loader->loadBias);
                        *patchAddr += loader->loadBias;
                        ptrMap_Set(&relrPatched, patchAddr, patchAddr);
                    }
                    offset += sizeof(uint32_t);
                    bitmap >>= 1;
                }
                base += 31 * sizeof(uint32_t);
            }
        }
        log_debug(plfELFLoader, "Processed %u bytes of DT_RELR packed relative relocations (%zu addresses)",
                  relrSz, relrPatched.count);
    }

    for (size_t i = 0; i < get_section_count(elf); ++i)
    {
        const Elf32_Shdr *sec = get_section(elf, i);
        bool isRel  = (sec->sh_type == SHT_REL);
        bool isRela = (sec->sh_type == SHT_RELA);
        if (!isRel && !isRela)
            continue;

        log_debug(plfELFLoader, "Found relocation section: %s", get_section_name(elf, i));

        /* Bulk-validated arrays: bounds checked ONCE per section instead of
         * per-entry via get_rel_entry()/get_symbol(). */
        const Elf32_Rel  *relArr  = NULL;
        const Elf32_Rela *relaArr = NULL;
        size_t relCount = 0;
        if (isRel)
            relArr = get_rel_array(elf, sec, &relCount);
        else
            relaArr = get_rela_array(elf, sec, &relCount);
        if ((!isRel || !relArr) && (!isRela || !relaArr))
            continue;
        log_debug(plfELFLoader, "  Entries: %zu", relCount);

        /* Resolve this relocation section's symbol table + strtab once. */
        size_t symCount = 0;
        const char *strTab = NULL;
        size_t strTabSize = 0;
        const Elf32_Sym *symArr = NULL;
        const Elf32_Shdr *symSec = get_section(elf, sec->sh_link);
        if (symSec)
            symArr = get_symbol_array(elf, symSec, &symCount, &strTab, &strTabSize);

        for (size_t j = 0; j < relCount; ++j)
        {
            uint32_t offset;
            uint32_t symbolIdx;
            uint32_t type;
            int32_t  addend = 0;

            if (isRel)
            {
                offset   = relArr[j].r_offset;
                symbolIdx = ELF32_R_SYM(relArr[j].r_info);
                type     = ELF32_R_TYPE(relArr[j].r_info);
            }
            else
            {
                offset   = relaArr[j].r_offset;
                symbolIdx = ELF32_R_SYM(relaArr[j].r_info);
                type     = ELF32_R_TYPE(relaArr[j].r_info);
                addend   = relaArr[j].r_addend;
            }

            const char *symbolName = "";
            uint32_t symSize  = 0;
            uint32_t symValue = 0;
            if (symArr && symbolIdx < symCount)
            {
                const Elf32_Sym *sym = &symArr[symbolIdx];
                symSize  = sym->st_size;
                symValue = sym->st_value;
                if (strTab && sym->st_name != 0 && sym->st_name < strTabSize)
                    symbolName = strTab + sym->st_name;
            }

            uint8_t *targetAddr = (uint8_t *)(uintptr_t)(offset + loader->loadBias);
            uint32_t *patchAddr = (uint32_t *)targetAddr;

            if (type == R_386_RELATIVE)
            {
                if (ptrMap_Get(&relrPatched, patchAddr) != NULL)
                {
                    log_trace(plfELFLoader, "Skipping R_386_RELATIVE at %p (already handled by DT_RELR)", patchAddr);
                    continue;
                }
                *patchAddr += loader->loadBias;
                log_trace(plfELFLoader, "Applied R_386_RELATIVE at %p (offset: 0x%x), new val: 0x%08X",
                          patchAddr, offset, *patchAddr);
                continue;
            }

            if (symbolName && symbolName[0])
            {
                void *resolvedFunc = symres_resolve_symbol(symbolName);

                if (resolvedFunc)
                {
                    uint32_t symVal = (uint32_t)(uintptr_t)resolvedFunc;
                    uint32_t inlineAddend = isRel ? *patchAddr : (uint32_t)addend;

                    switch (type)
                    {
                        case R_386_32:
                            *patchAddr = symVal + inlineAddend;
                            log_trace(plfELFLoader, "Applied R_386_32 for %s at %p (addend=%u -> val=0x%x)",
                                      symbolName, patchAddr, inlineAddend, *patchAddr);
                            break;
                        case R_386_PC32:
                            *patchAddr = symVal + inlineAddend - (uint32_t)(uintptr_t)patchAddr;
                            log_trace(plfELFLoader, "Applied R_386_PC32 for %s at %p (offset: 0x%x)",
                                      symbolName, patchAddr, *patchAddr);
                            break;
                        case R_386_COPY:
                        {
                            if (symSize > 0)
                            {
                                void *srcAddr = symres_resolve_symbol_in_shared_libs(symbolName);
                                if (!srcAddr && symValue != 0)
                                {
                                    void *candidate = (void *)(uintptr_t)(symValue + loader->loadBias);
                                    if (candidate != patchAddr)
                                        srcAddr = candidate;
                                }
                                if (!srcAddr)
                                {
                                    srcAddr = resolvedFunc;
                                }

                                if (srcAddr && srcAddr != patchAddr)
                                {
                                    memcpy(patchAddr, srcAddr, symSize);
                                    log_debug(plfELFLoader, "Applied R_386_COPY for %s (%u bytes) from %p to %p",
                                              symbolName, symSize, srcAddr, patchAddr);
                                    symbol_register(symbolName, patchAddr, NULL);
                                }
                                else
                                {
                                    log_warn(plfELFLoader, "R_386_COPY for %s: source not found (srcAddr=%p)",
                                              symbolName, srcAddr);
                                }
                            }
                            else
                            {
                                log_warn(plfELFLoader, "R_386_COPY for %s has 0 size", symbolName);
                            }
                            break;
                        }
                        case R_386_GLOB_DAT:
                        case R_386_JMP_SLOT:
                            *patchAddr = symVal;
                            log_trace(plfELFLoader, "Applied R_386_GLOB_DAT / R_386_JMP_SLOT for %s at %p",
                                      symbolName, patchAddr);
                            break;
                        default:
                            log_warn(plfELFLoader, "Unhandled relocation type %u for symbol %s", type, symbolName);
                            break;
                    }
                }
                else
                {
                    log_error(plfELFLoader, "Failed to resolve symbol %s and failed to create stub", symbolName);
                }
            }
            else
            {
                log_warn(plfELFLoader, "Unhandled anonymous relocation type %u at %p", type, patchAddr);
            }
        }
    }

    ptrMap_Destroy(&relrPatched);
    return true;
}

static bool export_symbols(ElfLoader *loader)
{
    log_debug(plfELFLoader, "Exporting dynamic symbols for resolution...");

    ElfFile *elf = &loader->elf;

    /* DYNSYM */
    size_t dynSymIdx = find_section_by_type(elf, SHT_DYNSYM);
    if (dynSymIdx != (size_t)-1)
    {
        const Elf32_Shdr *dynSymSec = get_section(elf, dynSymIdx);
        const char *strTab = NULL;
        size_t strTabSize = 0;
        size_t symCount = 0;
        const Elf32_Sym *symArr = get_symbol_array(elf, dynSymSec, &symCount, &strTab, &strTabSize);

        for (size_t k = 0; k < symCount; ++k)
        {
            const Elf32_Sym *sym = &symArr[k];
            unsigned char bind = ELF32_ST_BIND(sym->st_info);
            unsigned char type = ELF32_ST_TYPE(sym->st_info);

            if (sym->st_value == 0 || sym->st_shndx == 0 ||
                !(bind == STB_GLOBAL || bind == STB_WEAK || bind == 10) ||
                !(type == STT_FUNC || type == STT_OBJECT || type == 10))
                continue;

            const char *name = "";
            if (strTab && sym->st_name != 0 && sym->st_name < strTabSize)
                name = strTab + sym->st_name;

            void *symbolAddr = (void *)(uintptr_t)(sym->st_value + loader->loadBias);
            symres_register_native_symbol(name, symbolAddr);

            const char *atPos = strchr(name, '@');
            if (atPos)
            {
                char baseName[256];
                size_t baseLen = atPos - name;
                if (baseLen >= sizeof(baseName))
                    baseLen = sizeof(baseName) - 1;
                memcpy(baseName, name, baseLen);
                baseName[baseLen] = '\0';
                symres_register_native_symbol(baseName, symbolAddr);
            }
        }
    }
    else
    {
        log_warn(plfELFLoader, "No SHT_DYNSYM section found - nothing to export");
    }

    log_debug(plfELFLoader, "Exporting static symbols for findStaticFnAddr...");
    size_t symIdx = find_section_by_type(elf, SHT_SYMTAB);
    if (symIdx != (size_t)-1)
    {
        const Elf32_Shdr *symSec = get_section(elf, symIdx);
        const char *strTab = NULL;
        size_t strTabSize = 0;
        size_t symCount = 0;
        const Elf32_Sym *symArr = get_symbol_array(elf, symSec, &symCount, &strTab, &strTabSize);

        for (size_t k = 0; k < symCount; ++k)
        {
            const Elf32_Sym *sym = &symArr[k];
            unsigned char type = ELF32_ST_TYPE(sym->st_info);
            if (sym->st_value != 0 && sym->st_shndx != 0)
            {
                const char *name = "";
                if (strTab && sym->st_name != 0 && sym->st_name < strTabSize)
                    name = strTab + sym->st_name;

                if (type == STT_FUNC)
                {
                    void *symbolAddr = (void*)(uintptr_t)(sym->st_value + loader->loadBias);
                    symres_register_native_symbol(name, symbolAddr);
                }

                if (type == STT_OBJECT)
                {
                    void* symbolAddr = (void*)(uintptr_t)(sym->st_value + loader->loadBias);
                    symres_register_variable_symbol(name, symbolAddr);
                }
            }
        }
    }

    return true;
}

static bool resolve_vtables(ElfLoader *loader)
{
    log_debug(plfELFLoader, "Resolving VTables...");

    ElfFile *elf = &loader->elf;
    size_t secCount = get_section_count(elf);
    if (secCount == 0)
        return true;

    uint8_t *isTargetSection = (uint8_t *)calloc(secCount, sizeof(uint8_t));
    if (!isTargetSection)
        return true; /* allocation failure: skip VTable resolution gracefully */

    for (size_t i = 0; i < secCount; ++i)
    {
        const char *sname = get_section_name(elf, i);
        if (sname && (strcmp(sname, ".rodata") == 0 || strcmp(sname, ".data") == 0))
            isTargetSection[i] = 1;
    }

    for (size_t j = 0; j < secCount; ++j)
    {
        const Elf32_Shdr *symSec = get_section(elf, j);
        if (symSec->sh_type != SHT_SYMTAB && symSec->sh_type != SHT_DYNSYM)
            continue;

        const char *strTab = NULL;
        size_t strTabSize = 0;
        size_t symCount = 0;
        const Elf32_Sym *symArr = get_symbol_array(elf, symSec, &symCount, &strTab, &strTabSize);
        if (!symArr)
            continue;

        for (size_t k = 0; k < symCount; ++k)
        {
            const Elf32_Sym *sym = &symArr[k];
            if (sym->st_shndx < secCount &&
                isTargetSection[sym->st_shndx])
            {
                const char *name = "";
                if (strTab && sym->st_name != 0 && sym->st_name < strTabSize)
                    name = strTab + sym->st_name;
                if (strncmp(name, "_ZTV", 4) == 0 || strncmp(name, "_ZTT", 4) == 0)
                {
                    log_debug(plfELFLoader, "Found VTable/VTT symbol: %s at 0x%x", name, sym->st_value);
                    void *vtableAddr = (void *)(uintptr_t)(sym->st_value + loader->loadBias);
                    symbol_register(name, vtableAddr, NULL);
                }
            }
        }
    }

    free(isTargetSection);
    return true;
}

bool execute(ElfLoader *loader, int argc, char **argv, char **envp)
{
    log_debug(plfELFLoader, "Preparing to execute main ELF...");
    uint32_t entryAddr = get_entry(&loader->elf);
    if (entryAddr == 0)
    {
        log_error(plfELFLoader, "No entry point found - cannot execute");
        return false;
    }

    log_debug(plfELFLoader, "Entry point original address: 0x%x", entryAddr);

    void *actualEntry = (void *)(uintptr_t)(entryAddr + loader->loadBias);

    log_debug(plfELFLoader, "Executing Main ELF at mapped address: %p", actualEntry);
    ElfAuxvInfo auxvInfo;
    memset(&auxvInfo, 0, sizeof(auxvInfo));
    auxvInfo.AtPageSz = 4096;
    auxvInfo.AtEntry = (uint32_t)(uintptr_t)actualEntry;
    auxvInfo.AtPhent = 32;
    auxvInfo.AtPhnum = (uint32_t)get_segment_count(&loader->elf);

    for (size_t i = 0; i < get_segment_count(&loader->elf); ++i)
    {
        const Elf32_Phdr *seg = get_segment(&loader->elf, i);
        if (seg->p_type == PT_PHDR)
        {
            auxvInfo.AtPhdr = (uint32_t)(seg->p_vaddr + loader->loadBias);
            break;
        }
    }
    log_debug(plfELFLoader, "Auxv: AT_PHDR=0x%08X AT_PHENT=%u AT_PHNUM=%u AT_PAGESZ=%u AT_ENTRY=0x%08X",
              auxvInfo.AtPhdr, auxvInfo.AtPhent, auxvInfo.AtPhnum, auxvInfo.AtPageSz, auxvInfo.AtEntry);

    const uint32_t STACK_SIZE = 8 * 1024 * 1024;
    const uint32_t RESERVE_SIZE = 0x10000;

    uint32_t stackBase = 0;
    uint32_t stackLimit = 0;

    uint32_t esp = linuxStack_Setup(STACK_SIZE, argc, argv, &stackBase, &stackLimit, RESERVE_SIZE, &auxvInfo);

    __writefsdword(0x04, stackBase);
    __writefsdword(0x08, stackLimit);

    log_debug(plfELFLoader, "Stack Alloc: %p - %p", (void *)(uintptr_t)stackLimit, (void *)(uintptr_t)stackBase);

    typedef void (*EntryPointFunc)(void);
    EntryPointFunc entry = (EntryPointFunc)actualEntry;
    log_debug(plfELFLoader, "Jumping to entry point %p", entry);
    log_debug(plfELFLoader, "Stack ESP: 0x%08X", esp);

    //Sleep(100);

    __asm__ volatile("mov %0, %%esp\n\t"
                     "xor %%eax, %%eax\n\t"
                     "xor %%ebx, %%ebx\n\t"
                     "xor %%ecx, %%ecx\n\t"
                     "jmp *%1\n\t"
                     :
                     : "r"(esp), "r"(entry)
                     : "eax", "ebx", "ecx", "memory");

    __builtin_unreachable();
}

int hook_ret_zero(void)
{
    return 0;
}