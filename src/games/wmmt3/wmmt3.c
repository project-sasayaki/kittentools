#include <windows.h>
#include <stddef.h>
#include <stdlib.h>

#include "wmmt3.h"
#include "wmmt3System.h"
#include "wmmt3Graphics.h"
#include "wmmt3Kickback.h"
#include "wmmt3Input.h"
#include "wmmt3Ffb.h"
#include "../../config/config.h"
#include "../../elfLoader/libc.h"
#include "../../hooks/file.h"
#include "../../log/log.h"
#include "../../graphics/gpuVendor.h"
#include "../../elfLoader/symbol.h"
#include "../../audio/openal/openal.h"
#include "wmmt3Hasp.h"
#include "wmmt3Disk.h"
#include "../patchlib.h"
#include <MinHook.h>

bool wmmt3_hook(const char *name, void *replacement, void **next)
{
    void *target = get_native_symbol_ptr(name);
    return target && MH_CreateHook(target, replacement, next) == MH_OK;
}

static const WMMT3_InitialData WMMT3_JPN_V337_Data = {
    .gameID = "WM3100-1-NA-DAT0-A70",
    .verName = "Wangan Midnight Maximum Tune 3 (JPN)",
    .windowTitle = "WANGAN MIDNIGHT MAXIMUM TUNE 3",
};

static const WMMT3_InitialData WMMT3_EXP_Data = {
    .gameID = "WM3100-3-NA-DAT0-A70",
    .verName = "Wangan Midnight Maximum Tune 3 (EXP)",
    .windowTitle = "WANGAN MIDNIGHT MAXIMUM TUNE 3",
};

static const WMMT3_InitialData WMMT3DX_EXP_Data = {
    .gameID = "W3X100-4-NA-DAT0-A20",
    .verName = "Wangan Midnight Maximum Tune 3DX (EXP)",
    .windowTitle = "WANGAN MIDNIGHT MAXIMUM TUNE 3DX",
};

static const WMMT3_InitialData WMMT3DX_JPN_Data = {
    .gameID = "W3X100-1-NA-DAT0-C09",
    .verName = "Wangan Midnight Maximum Tune 3DX (JPN)",
    .windowTitle = "WANGAN MIDNIGHT MAXIMUM TUNE 3DX",
};

static const WMMT3_InitialData WMMT3DXP_JPN_Data = {
    .gameID = "W3P100-1-NA-DAT0-B02",
    .verName = "Wangan Midnight Maximum Tune 3DX+ (JPN)",
    .windowTitle = "WANGAN MIDNIGHT MAXIMUM TUNE 3DX PLUS",
};

/* FREE PLAY: answer clTestMode::isFreePlay. */
static void install_free_play(void)
{
    if (!Config.Coins.freeplay)
        return;
    if (!wmmt3_hook("_ZNK10clTestMode10isFreePlayEv", (void *)stubRetOne, NULL))
    {
        log_warn(plfWmmt3, "Free play: clTestMode::isFreePlay not found; using the game's own setting");
        return;
    }
    log_info(plfWmmt3, "Free play: on ([Coins] freeplay)");
}

/* Linked play is not emulated: report the network down so the boot never waits at the link check. */
static void install_standalone_boot(void)
{
    if (!wmmt3_hook("_ZN5clNet5isRunEv", (void *)stubRetZero, NULL))
        log_warn(plfWmmt3, "Stand-alone boot: clNet::isRun not found; the link check may wait forever");
}

static const WMMT3_InitialData WMMT3DX_EXP_C09_Data = {
    .gameID = "W3X100-4-NA-DAT0-C09",
    .verName = "Wangan Midnight Maximum Tune 3DX (EXP)",
    .windowTitle = "WANGAN MIDNIGHT MAXIMUM TUNE 3DX",
};

static const WMMT3_InitialData WMMT3DXP_EXP_Data = {
    .gameID = "W3P100-2-NA-DAT0-B02",
    .verName = "Wangan Midnight Maximum Tune 3DX+ (EXP)",
    .windowTitle = "WANGAN MIDNIGHT MAXIMUM TUNE 3DX PLUS",
};

static const WMMT3_InitialData WMMT3DXP_CHN_Data = {
    .gameID = "W3P100-5-NA-DAT0-A16",
    .verName = "Wangan Midnight Maximum Tune 3DX+ (CHN)",
    .windowTitle = "WANGAN MIDNIGHT MAXIMUM TUNE 3DX PLUS",
};

static const struct {
    uint32_t crc;
    const WMMT3_InitialData *data;
} g_WMMT3Versions[] = {
    {WMMT3_JPN_V337, &WMMT3_JPN_V337_Data},
    {WMMT3_EXP, &WMMT3_EXP_Data},
    {WMMT3DX_EXP, &WMMT3DX_EXP_Data},
    {WMMT3DX_EXP_C09, &WMMT3DX_EXP_C09_Data},
    {WMMT3DX_JPN, &WMMT3DX_JPN_Data},
    {WMMT3DXP_JPN, &WMMT3DXP_JPN_Data},
    {WMMT3DXP_CHN, &WMMT3DXP_CHN_Data},
    {WMMT3DXP_EXP, &WMMT3DXP_EXP_Data},
};

static const WMMT3_InitialData *init = NULL;

bool is_wmmt3_crc(uint32_t crc)
{
    for (size_t i = 0; i < ARRAYSIZE(g_WMMT3Versions); ++i)
    {
        if (g_WMMT3Versions[i].crc == crc)
            return true;
    }
    return false;
}

void init_wmmt3_prepatch(uint32_t crc)
{
    for (size_t i = 0; i < ARRAYSIZE(g_WMMT3Versions); ++i)
    {
        if (g_WMMT3Versions[i].crc == crc)
        {
            init = g_WMMT3Versions[i].data;
            break;
        }
    }

    log_warn(plfMisc, "  KittenTools By Project Sasayaki");
    log_warn(plfMisc, "  GAME:             %s", init ? init->verName : "UNKNOWN");
    log_warn(plfMisc, "  GAME ID:          %s", init ? init->gameID : "UNKNOWN");
    log_warn(plfMisc, "  GPU:              %s", getGPUVendorName());

    libc_set_system_handler(wmmt3_system);
    /* The cabinet keeps local time with TZ=UTC. */
    libc_use_local_time_as_utc(true);
    log_info(plfWmmt3, "Clock: the game sees the PC's local time (UTC%+d min), as on the cabinet",
             (int)(libc_wall_clock_offset() / 60));
    wmmt3_prepare_work_disk();
    /* .target.gz is full of 0x1A bytes. */
    file_use_linux_binary_semantics(true);

    wmmt3_configure_resolution();
    wmmt3_set_window_title(init ? init->windowTitle : "WANGAN MIDNIGHT MAXIMUM TUNE 3");
    wmmt3_install_graphics_hooks();
    wmmt3_install_kickback_hooks();
    wmmt3_input_init();
    wmmt3_ffb_init();
    install_free_play();
    openal_install_hooks(Config.wmmt3.openal_dll);
    wmmt3_install_disk_model();
    install_standalone_boot();
    wmmt3_install_hasp();
}
