#pragma once

#include <stddef.h>
#include <stdint.h>

#define amiCrc32RCalc(length, data) \
    getCrc32_JAMCRC((data), (length))

uint32_t getCrc32_JAMCRC(const uint8_t* data, size_t size);
uint32_t getCrc32Mem(const uint8_t* data, size_t size);
