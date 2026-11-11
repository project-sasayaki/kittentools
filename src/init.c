#include "config/config.h"
#include "graphics/gpuVendor.h"
#include "config/config.h"
#include <MinHook.h>
#include "log/log.h"
#include "games/games.h"

extern const GameModule* __start_game_modules[];
extern const GameModule* __stop_game_modules[];

static const char* g_guestArgument = NULL;

const char* KittenGuestArgument(void)
{
    return g_guestArgument;
}

void SetKittenGuestArgument(const char* arg)
{
    g_guestArgument = arg;
}

bool KittenInit(uint32_t crc)
{
    const GameModule* module;

    for (const GameModule** it = __start_game_modules; it < __stop_game_modules; ++it)
    {
        module = *it;

        if (module->detect(crc))
        {
            module->init();

            detectGPUType();

            if (MH_Initialize() != MH_OK)
            {
                log_error(plfBoot, "Failed to initialize MinHook");
                exit(1);
            }

            module->prepatch(crc);

            if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
            {
                log_error(plfBoot, "Failed to enable hooks");
                exit(1);
            }

            return true;
        }
    }

    return false;
}