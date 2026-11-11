#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <stdint.h>
#include <MinHook.h>

#include "wmmt3.h"
#include "../../init.h"
#include "../games.h"
#include "../config/config.h"
#include "../devices/baseBoard.h"
#include "../devices/motorboard.h"
#include "../devices/eeprom.h"
#include "../devices/device.h"
#include "../devices/strpcb.h"
#include "../devices/namcoJvio.h"
#include "../devices/namcoCardReader.h"
#include "../graphics/gpuVendor.h"
#include "../config/config.h"
#include "../log/log.h"

static void wmmt3_init(void)
{
    /* Without an argument main() runs as a development build. */
    SetKittenGuestArgument("1");

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
}

static const GameModule WMMT3_Module = {
    .name = "WMMT3",
    .detect = is_wmmt3_crc,
    .init = wmmt3_init,
    .prepatch = init_wmmt3_prepatch
};

MODULE_REGISTER(WMMT3_Module);