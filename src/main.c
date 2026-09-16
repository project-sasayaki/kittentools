#include <stdlib.h>
#include <string.h>

#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>
#include <libgen.h>
#include <dbghelp.h>

#include "elfLoader/segaapi.h"
#include "elfLoader/gcc.h"
#include "elfLoader/ipc.h"
#include "elfLoader/elfLoader.h"
#include "elfLoader/file.h"
#include "elfLoader/libc.h"
#include "elfLoader/graphics.h"
#include "elfLoader/pthread.h"
#include "elfLoader/termios.h"
#include "elfLoader/network.h"
#include "elfLoader/math.h"
#include "elfLoader/symbol.h"
#include "elfLoader/pthread/pthreadEmu.h"
#include "log/log.h"
#include "config/config.h"
#include "graphics/gpuVendor.h"
#include "graphics/internalRes.h"
#include "graphics/msaa.h"
#include "graphics/textureFilter.h"
#include "utils/Crc.h"
#include "init.h"
#include "cmd.h"

char g_absoluteElfPath[MAX_PATH_LENGTH];
char g_ElfName[MAX_PATH_LENGTH];

static void printStackTrace(CONTEXT *ctxPtr)
{
    HANDLE hProcess = GetCurrentProcess();
    HANDLE hThread  = GetCurrentThread();

    SymInitialize(hProcess, NULL, TRUE);

    CONTEXT ctx = *ctxPtr;
    STACKFRAME64 frame = {0};

    frame.AddrPC.Offset    = ctx.Eip;
    frame.AddrPC.Mode      = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Ebp;
    frame.AddrFrame.Mode   = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Esp;
    frame.AddrStack.Mode   = AddrModeFlat;

    FILE *crashLog = fopen("crash_stacktrace.log", "w");
    if (!crashLog) crashLog = stderr;

    fprintf(crashLog, "\n========== CRASH STACK TRACE ==========\n");

    MEMORY_BASIC_INFORMATION mbi;
    char eipMod[MAX_PATH] = "";
    if (VirtualQuery((LPCVOID)ctx.Eip, &mbi, sizeof(mbi)) && mbi.Type == MEM_IMAGE)
    {
        GetModuleFileNameA((HMODULE)mbi.AllocationBase, eipMod, sizeof(eipMod));
    }
    const char *eipBase = strrchr(eipMod, '\\');
    eipBase = eipBase ? eipBase + 1 : (eipMod[0] ? eipMod : "?");
    fprintf(crashLog, "  EIP: %s + 0x%llx\n", eipBase,
            (unsigned long long)ctx.Eip - (unsigned long long)(DWORD_PTR)mbi.AllocationBase);

    char symbolBuf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
    memset(symbolBuf, 0, sizeof(symbolBuf));
    SYMBOL_INFO *symbol = (SYMBOL_INFO *)symbolBuf;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen   = MAX_SYM_NAME;

    IMAGEHLP_LINE64 line;
    memset(&line, 0, sizeof(line));
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    int i;
    for (i = 0; i < 32; i++)
    {
        if (!StackWalk64(IMAGE_FILE_MACHINE_I386, hProcess, hThread, &frame, &ctx,
                         NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
            break;

        if (frame.AddrPC.Offset == 0)
            break;

        DWORD64 addr = frame.AddrPC.Offset;
        DWORD   displacement = 0;

        char lineBuf[128] = "";
        if (SymGetLineFromAddr64(hProcess, addr, &displacement, &line))
        {
            snprintf(lineBuf, sizeof(lineBuf), " (%s:%lu)",
                     line.FileName, line.LineNumber);
        }

        if (SymFromAddr(hProcess, addr, NULL, symbol))
        {
            DWORD64 disp = addr - symbol->Address;
            if (disp <= 0x10000)
            {
                fprintf(crashLog, "  #%02d  %s + 0x%llx%s\n",
                        i, symbol->Name, disp, lineBuf);
            }
            else
            {
                HMODULE hMod = (HMODULE)(DWORD_PTR)SymGetModuleBase64(hProcess, addr);
                char modName[MAX_PATH] = "";
                if (hMod && GetModuleFileNameA(hMod, modName, sizeof(modName)))
                {
                    const char *baseName = strrchr(modName, '\\');
                    baseName = baseName ? baseName + 1 : modName;
                    fprintf(crashLog, "  #%02d  %s + 0x%llx%s\n",
                            i, baseName, addr - (DWORD64)(DWORD_PTR)hMod, lineBuf);
                }
                else
                {
                    fprintf(crashLog, "  #%02d  0x%llx%s\n", i, addr, lineBuf);
                }
            }
        }
        else
        {
            HMODULE hMod = (HMODULE)(DWORD_PTR)SymGetModuleBase64(hProcess, addr);
            char modName[MAX_PATH] = "";
            if (hMod && GetModuleFileNameA(hMod, modName, sizeof(modName)))
            {
                const char *baseName = strrchr(modName, '\\');
                baseName = baseName ? baseName + 1 : modName;
                fprintf(crashLog, "  #%02d  %s + 0x%llx%s\n",
                        i, baseName, addr - (DWORD64)(DWORD_PTR)hMod, lineBuf);
            }
            else
            {
                fprintf(crashLog, "  #%02d  0x%llx%s\n", i, addr, lineBuf);
            }
        }
    }

    fprintf(crashLog, "========================================\n\n");
    fflush(crashLog);
    if (crashLog != stderr) fclose(crashLog);

    SymCleanup(hProcess);
}

static const char *exceptionName(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
    case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
    case 0xC0000409:                         return "STACK_BUFFER_OVERRUN (/GS)";
    case 0xC0000417:                         return "INVALID_PARAMETER";
    default:                                 return "UNKNOWN";
    }
}

LONG CALLBACK myVectoredHandler(PEXCEPTION_POINTERS ExceptionInfo)
{
    DWORD exceptionCode = ExceptionInfo->ExceptionRecord->ExceptionCode;
    PCONTEXT ctx = ExceptionInfo->ContextRecord;

    if (exceptionCode == EXCEPTION_ACCESS_VIOLATION || exceptionCode == EXCEPTION_PRIV_INSTRUCTION) {
        uint8_t *pint = (uint8_t *)ctx->Eip;
        if (pint && !IsBadReadPtr(pint, 2) && pint[0] == 0xCD && pint[1] == 0x80) {
            if (ctx->Eax == 0xE0) {
                ctx->Eax = (DWORD)pthreadEmu_pthreadSelf();
                ctx->Eip += 2;
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
    }
    else if (SUCCEEDED(exceptionCode))
        return EXCEPTION_CONTINUE_SEARCH;

    fprintf(stderr, "\n*** CRASH: %s (0x%08lX) at EIP=0x%08lX ***\n",
            exceptionName(exceptionCode), exceptionCode, ctx->Eip);
    printStackTrace(ctx);
    return EXCEPTION_CONTINUE_SEARCH;
}

void initBridges()
{
    symres_init();
    filesystemBridge_initBridges();
    graphicsBridge_initBridges();
    libcBridge_initBridges();
    pthreadBridge_initBridges();
    termiosBridge_initBridges();
    gccBridge_initBridges();
    networkBridge_initBridges();
    ipcBridge_initBridges();
    segaapiBridge_initBridges();
    mathBridge_initBridges();
}

int main(int argc, char *argv[], char *envp[])
{
    SetProcessDPIAware();
    timeBeginPeriod(1);

    uint32_t gameCrc32 = 0;
    char command[MAX_PATH_LENGTH] = {0};
    char gameELF[MAX_PATH_LENGTH] = {0};

    char elfPath[MAX_PATH_LENGTH];
    char elfArgs[MAX_PATH_LENGTH] = "";
    if (parseArgs(argc, argv, command, gameELF) != PARSE_ARGS_SUCCESS)
    {
        timeEndPeriod(1);
        return EXIT_SUCCESS;
    }
    load_config();
    logSetMinLevel(Config.sysconf.log_level);
    logInit();
    irInit();
    msInit();
    tfInit();

    char *spacePos = strchr(gameELF, ' ');
    if (spacePos != NULL)
    {
        size_t elfPathLen = (size_t)(spacePos - gameELF);
        strncpy(elfPath, gameELF, elfPathLen);
        elfPath[elfPathLen] = '\0';
        strcpy(elfArgs, spacePos + 1);
    }
    else
    {
        strcpy(elfPath, gameELF);
    }

    pre_reserve_address_space(elfPath);

    initBridges();

    if (!AddVectoredExceptionHandler(1, myVectoredHandler))
        log_error(plfBoot, "Failed to register MyVectoredHandler");

    strncpy(g_ElfName, elfPath, sizeof(g_ElfName) - 1);
    g_ElfName[sizeof(g_ElfName) - 1] = '\0';
    ElfLoader *loader = create();

    if (!load(loader, elfPath))
    {
        log_fatal(plfBoot, "Failed to load ELF file: %s", elfPath);
        timeEndPeriod(1);
        return 1;
    }

    uint8_t *baseAddr = (uint8_t *)get_base_address(loader);
    if (baseAddr)
    {
        gameCrc32 = getCrc32Mem(baseAddr + 10, 0x4000);
    }

    KittenInit(gameCrc32);

    int final_argc = 0;
    char *final_argv_arr[10];

    final_argv_arr[final_argc++] = elfPath;

    if (strlen(elfArgs) > 0)
    {
        char *token = strtok(elfArgs, " ");
        while (token != NULL && final_argc < 9)
        {
            final_argv_arr[final_argc++] = token;
            token = strtok(NULL, " ");
        }
    }
    final_argv_arr[final_argc] = NULL;

    if (!execute(loader, final_argc, final_argv_arr, envp))
    {
        log_error(plfBoot, "Failed to execute ELF file.");
        timeEndPeriod(1);
        return 1;
    }

    destroy(loader);
    timeEndPeriod(1);
    return 0;
}