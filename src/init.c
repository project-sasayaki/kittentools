#include "config/config.h"
#include "devices/baseBoard.h"
#include "devices/motorboard.h"
#include "devices/eeprom.h"
#include "devices/device.h"
#include "graphics/gpuVendor.h"
#include "config/config.h"
#include <MinHook.h>
#include "log/log.h"
#include "games/idas/idas.h"
#include "hooks/amLib.h"


void KittenInit(uint32_t crc)
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

    detectGPUType();

    if (MH_Initialize() != MH_OK)
    {
        log_error(plfBoot, "Failed to initialize MinHook");
        exit(1);
    }

    InitAmLibHooks();
    init_idas_prepatch(crc);

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        log_error(plfBoot, "Failed to enable hooks");
        exit(1);
    }
}