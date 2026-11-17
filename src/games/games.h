#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct GameModule {
    const char* name;
    bool (*detect)(uint32_t crc32);
    void (*init)(void);
    void (*prepatch)(uint32_t crc32);
} GameModule;

#define MODULE_REGISTER(module) \
    static const GameModule * const module##_registration \
    __attribute__((used, section("game_modules"))) = &(module)
