#pragma once

#include <stdint.h>

#pragma pack(push, 1)

typedef struct
{
    char     m_GameId[4];         /* 0x00 - 0x03 */
    uint8_t  m_MajorRelease;      /* 0x04 */
    uint8_t  m_ModelType;         /* 0x05 */
    uint8_t  m_Rsv06[25];         /* 0x06 - 0x1E */
    uint8_t  m_FormatType;        /* 0x1F */
    uint8_t  m_SrcMedia[4];       /* 0x20 - 0x23 */
    uint8_t  m_DstMedia[4];       /* 0x24 - 0x27 */
    uint32_t m_NfsServerIp;       /* 0x28 - 0x2B */
    int8_t   m_AppPartSize;       /* 0x2C */
    uint8_t  m_FdcType;           /* 0x2D */
    uint8_t  Rsv02C[18];          /* 0x2E - 0x3F */
} m_Boot;

typedef struct
{
    uint32_t m_Flag;         // DHCP
    uint32_t m_IpAddress;    // 0x04
    uint32_t m_SubnetMask;   // 0x08
    uint32_t m_Gateway;      // 0x0C
    uint32_t m_PrimaryDns;   // 0x10
    uint32_t m_SecondaryDns; // 0x14
} AM_SYSDATAwH_NETWORK;

#pragma pack(pop)

int InitAmLibHooks();
void setupGameId(const char* _pcGameId);