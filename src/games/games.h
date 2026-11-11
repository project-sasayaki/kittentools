#pragma once
#include <stdint.h>

typedef struct GameModule {
    const char* name;
    int (*detect)(uint32_t crc32);
    int (*init)(void);
    int (*prepatch)(void);
} GameModule;

#define MODULE_REGISTER(module) \
    static const GameModule * const module##_registration \
    __attribute__((used, section("game_modules"))) = &(module)