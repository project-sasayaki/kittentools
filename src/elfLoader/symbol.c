#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <winsock2.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

#include "collections.h"
#include "memory.h"
#include "elfLoader.h"
#include "symbol.h"
#include "../log/log.h"
#include "libc.h"


extern void *GLHooks_GetProcAddress(const char *procName);
extern void PthreadEmu_Initialize(void);
extern void PthreadEmu_Shutdown(void);


extern char g_ElfName[1024];


static StrMap   g_VTables;            
static StrMap   g_NativeSymbols;
static StrMap   g_VarSymbols;
static StrMap   g_LibraryMap;         
static StrMap   g_OriginalSymbolPtrs; 

static PtrList  g_LoadedLibraries;

void __gmon_start__(void){}

int __libc_start_main(int (*main_func)(int, char **, char **), int argc, char **argv,
                      void (*init)(void), void (*fini)(void),
                      void (*rtld_fini)(void), void *stack_end)
{
    PthreadEmu_Initialize();
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    init();

    char **envp = argv + argc + 1;

    KittenSetLogBasename(g_ElfName);

    int exitCode = main_func(argc, argv, envp);
    log_debug(plfSymbolResolv, "Program main() exited with code: %d", exitCode);

    fini();

    PthreadEmu_Shutdown();
    exit(exitCode);
    return exitCode;
}

void handle_unresolved_symbol(const char *symbolName, void *callerAddress)
{
    log_fatal(plfSymbolResolv, "\n*** FATAL ERROR ***\nUnresolved symbol called: %s\nCaller Address: %p\n*******************\n",
              symbolName, callerAddress);
    fflush(stdout);

    char msg[512];
    snprintf(msg, sizeof(msg), "Unresolved symbol called: %s\nCaller Address: %p\n",
             symbolName, callerAddress);
    MessageBoxA(NULL, msg, "Fatal Error", MB_ICONERROR | MB_OK);
    ExitProcess(1);
}

static void *create_unresolved_stub(const char *symbolName)
{
    static uint8_t *currentBlock = NULL;
    static size_t offset = 0;

    if (!currentBlock || offset + 18 > 4096)
    {
        currentBlock = (uint8_t *)allocate_executable(4096, NULL);
        offset = 0;
    }

    if (!currentBlock)
        return NULL;

    uint8_t *stub = currentBlock + offset;
    offset += 18;

    size_t nameLen = strlen(symbolName) + 1;
    char *namePtr = NULL;

    if (offset + nameLen <= 4096)
    {
        namePtr = (char *)(currentBlock + offset);
        offset += nameLen;
    }
    else
    {
        currentBlock = (uint8_t *)allocate_executable(4096, NULL);
        if (!currentBlock)
            return NULL;
        stub = currentBlock;
        offset = 18;
        namePtr = (char *)(currentBlock + offset);
        offset += nameLen;
    }

    strcpy(namePtr, symbolName);

    /* mov eax, [esp]  ; push eax  ; push namePtr  ; call HandleUnresolvedSymbol  ; add esp,8  ; ret */
    stub[0] = 0x8B;
    stub[1] = 0x04;
    stub[2] = 0x24;
    stub[3] = 0x50;
    stub[4] = 0x68;
    *(uint32_t *)(stub + 5) = (uint32_t)(uintptr_t)namePtr;
    stub[9] = 0xE8;
    uint32_t callTarget = (uint32_t)(uintptr_t)&handle_unresolved_symbol;
    uint32_t relOffset = callTarget - ((uint32_t)(uintptr_t)stub + 14);
    *(uint32_t *)(stub + 10) = relOffset;
    stub[14] = 0x83;
    stub[15] = 0xC4;
    stub[16] = 0x08;
    stub[17] = 0xC3;

    return stub;
}


void symres_init(void)
{
    strMap_Init(&g_VTables);
    strMap_Init(&g_NativeSymbols);
    strMap_Init(&g_VarSymbols);
    strMap_Init(&g_LibraryMap);
    strMap_Init(&g_OriginalSymbolPtrs);
    ptrList_Init(&g_LoadedLibraries);

    /* Register default libc / memory symbols */
    symbol_register("__gmon_start__", __gmon_start__, NULL);
    symbol_register("__libc_start_main", __libc_start_main, NULL);

    symbol_register("malloc",        hook_malloc, NULL);
    symbol_register("mallinfo",      hook_mallinfo, NULL);
    symbol_register("calloc",        hook_calloc, NULL);
    symbol_register("realloc",       hook_realloc, NULL);
    symbol_register("free",          hook_free, NULL);
    symbol_register("memmove",       memmove, NULL);
    symbol_register("wmemmove",      wmemmove, NULL);
    symbol_register("memalign",      hook_memalign, NULL);
    symbol_register("posix_memalign", hook_posix_memalign, NULL);
    symbol_register("strndup",       hook_strndup, NULL);
    symbol_register("strdup",        hook_strdup, NULL);
    symbol_register("__strdup",      hook_strdup, NULL);
    symbol_register("strcoll",       strcoll, NULL);
    symbol_register("strxfrm",       strxfrm, NULL);
    symbol_register("memcpy",        memcpy, NULL);
    symbol_register("memset",        memset, NULL);
    symbol_register("memcmp",        memcmp, NULL);
    symbol_register("memchr",        memchr, NULL);
    symbol_register("strlen",        strlen, NULL);
    symbol_register("strcpy",        strcpy, NULL);
    symbol_register("strncpy",       strncpy, NULL);
    symbol_register("strcmp",        strcmp, NULL);
    symbol_register("strncmp",       strncmp, NULL);
    symbol_register("strchr",        strchr, NULL);
    symbol_register("strrchr",       strrchr, NULL);
    symbol_register("strstr",        strstr, NULL);
    symbol_register("strtol",        strtol, NULL);
    symbol_register("strtod",        strtod, NULL);
    symbol_register("strerror",      strerror, NULL);
    symbol_register("strtok",        strtok, NULL);
    symbol_register("strtok_r",      strtok_r, NULL);
    symbol_register("strncat",       strncat, NULL);
    symbol_register("strcat",        strcat, NULL);
    symbol_register("strpbrk",       strpbrk, NULL);
    symbol_register("strcspn",       strcspn, NULL);
    symbol_register("strspn",        strspn, NULL);
    symbol_register("strcasecmp",    strcasecmp, NULL);
    symbol_register("strncasecmp",   strncasecmp, NULL);
    symbol_register("mblen",         mblen, NULL);
    symbol_register("putc",          putc, NULL);
    symbol_register("putchar",       putchar, NULL);
    symbol_register("fputc",         fputc, NULL);
}

void symbol_register(const char *name, void *func_ptr, void **next_ptr)
{
    strMap_Set(&g_VTables, name, func_ptr);
    if (next_ptr)
        strMap_Set(&g_OriginalSymbolPtrs, name, next_ptr);

    log_debug(plfSymbolResolv, "Registered symbol: %s at %p", name, func_ptr);
}

void symres_register_native_symbol(const char *name, void *symbolPtr)
{
    if (!strMap_Has(&g_NativeSymbols, name))
    {
        strMap_Set(&g_NativeSymbols, name, symbolPtr);
        log_trace(plfSymbolResolv, "Exported native symbol: %s at %p", name, symbolPtr);
    }
}

void* get_native_symbol_ptr(const char *name)
{
    void* vt = strMap_Get(&g_NativeSymbols, name);

    if (!vt)
        return NULL;

    log_trace(plfSymbolResolv, "%s found at %p", name, vt);
    return vt;
}

void symres_register_variable_symbol(const char *name, void *symbolPtr)
{
    if (!strMap_Has(&g_VarSymbols, name))
    {
        strMap_Set(&g_VarSymbols, name, symbolPtr);
        log_trace(plfSymbolResolv, "Exported variable symbol: %s at %p", name, symbolPtr);
    }
}

void* get_variable_symbol_ptr(const char *name)
{
    void* vt = strMap_Get(&g_VarSymbols, name);

    if (!vt)
        return NULL;

    log_trace(plfSymbolResolv, "%s found at %p", name, vt);
    return vt;
}

void symres_register_library(const char *linuxName, const char *windowsName)
{
    strMap_Set(&g_LibraryMap, linuxName, (void *)windowsName);
    log_trace(plfSymbolResolv, "Registered library substitution: %s -> %s", linuxName, windowsName);
}

void *symres_get_vtable(const char *className)
{
    return strMap_Get(&g_VTables, className);
}

void *symres_resolve_symbol(const char *symbolName)
{
    log_trace(plfSymbolResolv, "Resolving symbol: %s", symbolName);
    void *resolvedAddr = NULL;
    void *originalAddr = NULL;

    if (strncmp(symbolName, "__gmon_start__", 14) == 0 ||
        strncmp(symbolName, "_ITM_deregisterTMCloneTable", 27) == 0 ||
        strncmp(symbolName, "_ITM_registerTMCloneTable", 27) == 0 ||
        strncmp(symbolName, "_Jv_RegisterClasses", 19) == 0 ||
        strncmp(symbolName, "__cxa_finalize", 14) == 0)
    {
        log_debug(plfSymbolResolv, "Resolved weak symbol '%s' to NULL (Not Implemented/Required)", symbolName);
        return (void *)(uintptr_t)&hook_ret_zero;
    }

    if (strncmp(symbolName, "_Unwind_", 8) == 0 ||
        strcmp(symbolName, "__gxx_personality_v0") == 0)
    {
        return (void *)(uintptr_t)&hook_ret_zero;
    }

    size_t symLen = strlen(symbolName);

    void *vtAddr = strMap_GetWithLen(&g_VTables, symbolName, symLen);
    if (vtAddr)
    {
        resolvedAddr = vtAddr;
    }

    int needsOriginal = (!resolvedAddr) || (strMap_GetWithLen(&g_OriginalSymbolPtrs, symbolName, symLen) != NULL);

    if (needsOriginal)
    {
        void *nativeAddr = strMap_GetWithLen(&g_NativeSymbols, symbolName, symLen);
        if (nativeAddr)
        {
            originalAddr = nativeAddr;
        }

        if (!originalAddr)
        {
            for (size_t i = 0; i < g_LoadedLibraries.count; ++i)
            {
                HMODULE handle = (HMODULE)g_LoadedLibraries.items[i];
                if (!handle)
                    continue;

                void *symbolAddr = (void *)(uintptr_t)GetProcAddress(handle, symbolName);
                if (symbolAddr)
                {
                    originalAddr = symbolAddr;
                    break;
                }
            }
        }

        if (!originalAddr)
        {
            if (strncmp(symbolName, "glX", 3) == 0)
            {
                void *proc = symres_get_vtable(symbolName);
                if (proc)
                {
                    log_trace(plfSymbolResolv, "Symbol %s resolved from GLXBridge at 0x%p", symbolName, proc);
                    originalAddr = proc;
                }
            }
            else if (strncmp(symbolName, "gl", 2) == 0)
            {
                void *proc = GLHooks_GetProcAddress(symbolName);
                if (proc)
                {
                    log_trace(plfSymbolResolv, "Symbol %s resolved from GLHooks at 0x%p", symbolName, proc);
                    originalAddr = proc;
                }
            }
        }

        if (originalAddr)
        {
            void **origPtr = (void **)strMap_GetWithLen(&g_OriginalSymbolPtrs, symbolName, symLen);
            if (origPtr)
            {
                *origPtr = originalAddr;
            }
        }
    }

    if (resolvedAddr)
    {
        log_trace(plfSymbolResolv, "Symbol %s resolved at 0x%p (hooked)", symbolName, resolvedAddr);
        return resolvedAddr;
    }

    if (originalAddr)
    {
        log_trace(plfSymbolResolv, "Symbol %s resolved at 0x%p (original)", symbolName, originalAddr);
        return originalAddr;
    }

    log_warn(plfSymbolResolv, "Symbol not found: %s. Generating crash stub.", symbolName);
    return create_unresolved_stub(symbolName);
}

void *symres_resolve_symbol_in_shared_libs(const char *symbolName)
{
    for (size_t i = 0; i < g_LoadedLibraries.count; ++i)
    {
        HMODULE handle = (HMODULE)g_LoadedLibraries.items[i];
        if (!handle)
            continue;
        void *symbolAddr = (void *)(uintptr_t)GetProcAddress(handle, symbolName);
        if (symbolAddr)
        {
            log_trace(plfSymbolResolv, "%s found at %p", symbolName, symbolAddr);
            return symbolAddr;
        }
    }

    void *vt = strMap_Get(&g_VTables, symbolName);
    if (!vt)
        return NULL;

    log_trace(plfSymbolResolv, "%s found at %p", symbolName, vt);
    return vt;
}

void symres_load_needed_library(const char *linuxName)
{
    const char *targetName = (const char *)strMap_Get(&g_LibraryMap, linuxName);
    if (!targetName)
        return;

    HMODULE handle = LoadLibraryA(targetName);
    if (handle)
    {
        if (ptrList_Find(&g_LoadedLibraries, (void *)handle) < 0)
            ptrList_Append(&g_LoadedLibraries, (void *)handle);
        log_debug(plfSymbolResolv, "Loaded library: %s", targetName);
        return;
    }

    log_error(plfSymbolResolv, "Failed to load library: %s", targetName);
}

void *hook_resolve_symbol(const char *symbolName)
{
    return symres_resolve_symbol(symbolName);
}

void hook_load_needed_library(const char *filename)
{
    symres_load_needed_library(filename);
}