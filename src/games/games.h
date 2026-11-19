#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "../config/config.h"

typedef struct GameModule {
    const char* name;
    bool (*detect)(uint32_t crc32);
    void (*init)(void);
    void (*prepatch)(uint32_t crc32);
    /* What kitten/config.ini holds for this game: the ConfigGroups of config.def it uses, and its
     * own settings (NULL = none), which are read once the game is recognised. */
    unsigned sharedConfig;
    const ConfigTable *config;
    /* The GROUP()s of its own settings that the version with this CRC has (NULL = it has no GROUP()). */
    unsigned (*configGroups)(uint32_t crc32);
} GameModule;

#define MODULE_REGISTER(module) \
    static const GameModule * const module##_registration \
    __attribute__((used, section("game_modules"))) = &(module)
