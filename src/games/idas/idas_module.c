#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <stdint.h>
#include <MinHook.h>

#include "idas.h"
#include "../games.h"
#include "../config/config.h"
#include "../devices/baseBoard.h"
#include "../devices/motorboard.h"
#include "../devices/eeprom.h"
#include "../devices/device.h"
#include "../devices/strpcb.h"
#include "../graphics/gpuCompat.h"
#include "../log/log.h"
#include "../hooks/amLib.h"

bool init_idas_detect(uint32_t crc)
{
    for (size_t i = 0; i < ARRAYSIZE(g_IDASVersions); ++i) {
        if (g_IDASVersions[i].crc == crc)
            return true;
    }
    return false;
}

void init_idas(void)
{
    DeviceDescriptor baseboard = {
        .fd = -1,
        .emulate = 1,
        .mountPoint = "/dev/lbb",
        .redirectPoint = "NUL",
        .type = DEVICE_TYPE_BASEBOARD,
        .name = "baseboard",
        .init = initBaseboard,
        .ioctl = baseboardIoctl,
        .read = baseboardRead,
        .write = baseboardWrite
    };

    DeviceDescriptor eeprom = {
        .fd = -1,
        .emulate = 1,
        .type = DEVICE_TYPE_EEPROM,
        .mountPoint = "/dev/i2c/0",
        .redirectPoint = "NUL",
        .name = "eeprom",
        .init = initEeprom,
        .ioctl = eepromIoctl,
    };
    DeviceDescriptor driveborad = {
        .fd = -1,
        .emulate = 1,
        .type = DEVICE_TYPE_SERIAL0,
        .mountPoint = "/dev/tts/0",
        .redirectPoint = "NUL",
        .name = "motorboard",
        .write = motorboardWrite,
    };
    log_debug(plfBoot, "Initializing main...");
    InitDevices();
    attachDevice(&baseboard);
    attachDevice(&eeprom);
    attachDevice(&driveborad);
}

static const GameModule IDAS_Module = {
    .name = "IDAS",
    .detect = init_idas_detect,
    .init = init_idas,
    .prepatch = init_idas_prepatch,
    .sharedConfig = CONFIG_GROUP_DISPLAY | CONFIG_GROUP_NETWORK | CONFIG_GROUP_SYSCONF |
                    CONFIG_GROUP_COINS | CONFIG_GROUP_PATH | CONFIG_GROUP_IO3 | CONFIG_GROUP_FFB |
                    CONFIG_GROUP_XINPUT | CONFIG_GROUP_DINPUT,
    /* Cg 1.4/1.5 compiles the game's shaders at run time; the compiler is patched
     * so its output loads on AMD/Intel, and the GL calls are routed to match. */
    .gpuCompat = GPU_COMPAT_CG_ARB_PROFILES | GPU_COMPAT_GENERIC_ATTRIBS |
                 GPU_COMPAT_CONSTANT_BANKS | GPU_COMPAT_PINNED_UNIFORMS |
                 GPU_COMPAT_INTEL_SHADOW | GPU_COMPAT_NV_ASM_RECOMPILE |
                 GPU_COMPAT_SKIP_ASM_CACHE,
};

MODULE_REGISTER(IDAS_Module);