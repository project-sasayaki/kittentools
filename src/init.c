#include "config/config.h"
#include "devices/baseBoard.h"
#include "devices/motorboard.h"
#include "devices/eeprom.h"
#include "devices/device.h"
#include "devices/strpcb.h"
#include "devices/namcoJvio.h"
#include "devices/namcoCardReader.h"
#include "graphics/gpuVendor.h"
#include "config/config.h"
#include <MinHook.h>
#include "log/log.h"
#include "games/idas/idas.h"
#include "games/wmmt3/wmmt3.h"
#include "hooks/amLib.h"


static const char *g_guestArgument;

const char *KittenGuestArgument(void)
{
    return g_guestArgument;
}

static void KittenInitWmmt3(uint32_t crc)
{
    /* Without an argument main() runs as a development build. */
    g_guestArgument = "1";

    DeviceDescriptor strpcb = {
        .fd = -1,
        .emulate = 1,
        .mountPoint = "/dev/ttyM1",
        .redirectPoint = "NUL",
        .type = DEVICE_TYPE_SERIAL1,
        .name = "strpcb",
        .init = strpcbInit,
        .ioctl = strpcbIoctl,
        .read = strpcbRead,
        .write = strpcbWrite,
        .close = strpcbClose,
    };

    DeviceDescriptor jvio = {
        .fd = -1,
        .emulate = 1,
        .mountPoint = "/dev/ttyM3",
        .redirectPoint = "NUL",
        .type = DEVICE_TYPE_SERIAL0,
        .name = "jvio",
        .init = namcoJvioInit,
        .ioctl = namcoJvioIoctl,
        .read = namcoJvioRead,
        .write = namcoJvioWrite,
        .close = namcoJvioClose,
    };

    DeviceDescriptor cardReader = {
        .fd = -1,
        .emulate = 1,
        .mountPoint = "/dev/ttyM2",
        .redirectPoint = "NUL",
        .type = DEVICE_TYPE_PCI_CARD_000,
        .name = "card reader",
        .init = namcoCardReaderInit,
        .ioctl = namcoCardReaderIoctl,
        .read = namcoCardReaderRead,
        .write = namcoCardReaderWrite,
        .close = namcoCardReaderClose,
    };

    log_debug(plfBoot, "Initializing main...");
    InitDevices();
    attachDevice(&strpcb);
    attachDevice(&jvio);
    attachDevice(&cardReader);

    detectGPUType();

    if (MH_Initialize() != MH_OK)
    {
        log_error(plfBoot, "Failed to initialize MinHook");
        exit(1);
    }

    init_wmmt3_prepatch(crc);

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        log_error(plfBoot, "Failed to enable hooks");
        exit(1);
    }
}

void KittenInit(uint32_t crc)
{
    if (is_wmmt3_crc(crc))
    {
        KittenInitWmmt3(crc);
        return;
    }

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