#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct ElfLoader;
typedef struct ElfLoader ElfLoader;

void symres_init(void);
void symbol_register(const char *name, void *funcPtr, void **originalSymbolPtr);
void symres_register_native_symbol(const char *name, void *symbolPtr);
void *get_native_symbol_ptr(const char *name);
void symres_register_variable_symbol(const char *name, void *symbolPtr);
void *get_variable_symbol_ptr(const char *name);
void symres_register_library(const char *linuxName, const char *windowsName);
void *symres_resolve_symbol(const char *symbolName);
void *symres_resolve_symbol_in_shared_libs(const char *symbolName);
void *symres_get_vtable(const char *className);
void symres_load_needed_library(const char *linuxName);
void *hook_resolve_symbol(const char *symbolName);
void hook_load_needed_library(const char *filename);
