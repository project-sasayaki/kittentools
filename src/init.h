#pragma once

bool KittenInit(uint32_t crc);
void KittenCreateConfig(void);

const char *KittenGuestArgument(void);
void SetKittenGuestArgument(const char* arg);