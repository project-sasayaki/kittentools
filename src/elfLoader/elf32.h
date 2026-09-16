#pragma once


#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>


typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;
typedef int32_t  Elf32_Sword;
typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;

/* e_ident indices */
#define EI_MAG0     0
#define EI_MAG1     1
#define EI_MAG2     2
#define EI_MAG3     3
#define EI_CLASS    4
#define EI_DATA     5
#define EI_NIDENT   16

/* Magic */
#define ELFMAG0     0x7f
#define ELFMAG1     'E'
#define ELFMAG2     'L'
#define ELFMAG3     'F'

/* Class */
#define ELFCLASSNONE 0
#define ELFCLASS32   1
#define ELFCLASS64   2

/* Data encoding */
#define ELFDATANONE  0
#define ELFDATA2LSB  1
#define ELFDATA2MSB  2

/* File types */
#define ET_NONE     0
#define ET_REL      1
#define ET_EXEC     2
#define ET_DYN      3

/* Machine types */
#define EM_386      3

/* Program header types */
#define PT_NULL     0
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3
#define PT_NOTE     4
#define PT_SHLIB    5
#define PT_PHDR     6

/* Section header types */
#define SHT_NULL        0
#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_RELA        4
#define SHT_HASH        5
#define SHT_DYNAMIC     6
#define SHT_NOTE        7
#define SHT_NOBITS      8
#define SHT_REL         9
#define SHT_SHLIB       10
#define SHT_DYNSYM      11

/* Symbol binding */
#define STB_LOCAL    0
#define STB_GLOBAL   1
#define STB_WEAK     2

/* Symbol type */
#define STT_NOTYPE   0
#define STT_OBJECT   1
#define STT_FUNC     2
#define STT_SECTION  3
#define STT_FILE     4

/* Dynamic tags */
#define DT_NULL          0
#define DT_NEEDED        1
#define DT_PLTRELSZ      2
#define DT_PLTGOT        3
#define DT_HASH          4
#define DT_STRTAB        5
#define DT_SYMTAB        6
#define DT_RELA          7
#define DT_RELASZ        8
#define DT_RELAENT       9
#define DT_STRSZ         10
#define DT_SYMENT        11
#define DT_INIT          12
#define DT_FINI          13
#define DT_SONAME        14
#define DT_RPATH         15
#define DT_SYMBOLIC      16
#define DT_REL           17
#define DT_RELSZ         18
#define DT_RELENT        19
#define DT_PLTREL        20
#define DT_DEBUG         21
#define DT_TEXTREL       22
#define DT_JMPREL        23
#define DT_BIND_NOW      24
#define DT_INIT_ARRAY    25
#define DT_FINI_ARRAY    26
#define DT_INIT_ARRAYSZ  27
#define DT_FINI_ARRAYSZ  28
#define DT_RUNPATH       29
#define DT_FLAGS         30
#define DT_RELRSZ        35
#define DT_RELR          36
#define DT_RELRENT       37

/* x86 relocation types */
#define R_386_NONE       0
#define R_386_32         1
#define R_386_PC32       2
#define R_386_GOT32      3
#define R_386_PLT32      4
#define R_386_COPY       5
#define R_386_GLOB_DAT   6
#define R_386_JMP_SLOT   7
#define R_386_RELATIVE   8

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
} Elf32_Phdr;

typedef struct {
    Elf32_Word sh_name;
    Elf32_Word sh_type;
    Elf32_Word sh_flags;
    Elf32_Addr sh_addr;
    Elf32_Off  sh_offset;
    Elf32_Word sh_size;
    Elf32_Word sh_link;
    Elf32_Word sh_info;
    Elf32_Word sh_addralign;
    Elf32_Word sh_entsize;
} Elf32_Shdr;

typedef struct {
    Elf32_Word st_name;
    Elf32_Addr st_value;
    Elf32_Word st_size;
    unsigned char st_info;
    unsigned char st_other;
    Elf32_Half st_shndx;
} Elf32_Sym;

typedef struct {
    Elf32_Addr r_offset;
    Elf32_Word r_info;
} Elf32_Rel;

typedef struct {
    Elf32_Addr  r_offset;
    Elf32_Word  r_info;
    Elf32_Sword r_addend;
} Elf32_Rela;

typedef struct {
    Elf32_Sword d_tag;
    union {
        Elf32_Word d_val;
        Elf32_Addr d_ptr;
    } d_un;
} Elf32_Dyn;

/* Relocation / symbol decode macros */
#define ELF32_R_SYM(i)     ((i) >> 8)
#define ELF32_R_TYPE(i)    ((unsigned char)((i) & 0xff))
#define ELF32_R_INFO(s, t) (((s) << 8) + (unsigned char)(t))

#define ELF32_ST_BIND(i)   ((i) >> 4)
#define ELF32_ST_TYPE(i)   ((i) & 0xf)
#define ELF32_ST_INFO(b, t) (((b) << 4) + ((t) & 0xf))

typedef struct {
    Elf32_Ehdr   ehdr;
    Elf32_Phdr  *phdrs;      /* e_phnum entries (heap-allocated copy) */
    Elf32_Shdr  *shdrs;      /* e_shnum entries (heap-allocated copy) */
    const char  *shstrtab;   /* pointer into rawData (section name table) */
    size_t       shstrtabSize;
    char        *rawData;    /* entire file contents (malloc'd) */
    size_t       rawSize;
} ElfFile;

/* Load and parse an ELF file. Returns true on success. */
bool load_elf32(ElfFile *elf, const char *path);
void free_elf32(ElfFile *elf);

/* Header accessors */
uint16_t get_class(const ElfFile *elf);
uint16_t get_machine(const ElfFile *elf);
uint16_t get_type(const ElfFile *elf);
uint32_t get_entry(const ElfFile *elf);

/* Program header access */
size_t get_segment_count(const ElfFile *elf);
const Elf32_Phdr *get_segment(const ElfFile *elf, size_t i);

/* Section header access */
size_t get_section_count(const ElfFile *elf);
const Elf32_Shdr *get_section(const ElfFile *elf, size_t i);
const char    *get_section_name(const ElfFile *elf, size_t i);
const Elf32_Rel  *get_rel_array(const ElfFile *elf, const Elf32_Shdr *sec, size_t *outCount);
const Elf32_Rela *get_rela_array(const ElfFile *elf, const Elf32_Shdr *sec, size_t *outCount);
const Elf32_Sym  *get_symbol_array(const ElfFile *elf, const Elf32_Shdr *sec,
                                   size_t *outCount, const char **outStrTab, size_t *outStrTabSize);
const Elf32_Dyn  *get_dynamic_array(const ElfFile *elf, const Elf32_Shdr *sec, size_t *outCount);

/* Helper: find first section by type. Returns index or (size_t)-1. */
size_t find_section_by_type(const ElfFile *elf, Elf32_Word type);
