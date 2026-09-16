#include "Crc.h"

#define CRC32_POLYNOMIAL 0xedb88320

uint32_t calcCrc32(uint32_t crc, uint8_t data)
{
    crc ^= data;
    for (int i = 0; i < 8; i++)
    {
        if (crc & 1)
        {
            crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
        }
        else
        {
            crc = (crc >> 1);
        }
    }
    return crc;
}

uint32_t getCrc32_JAMCRC(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; i++)
    {
        crc = calcCrc32(crc, data[i]);
    }
    return crc;
}

uint32_t getCrc32Mem(const uint8_t* data, size_t size)
{
    return getCrc32_JAMCRC(data, size) ^ 0xFFFFFFFF;
}