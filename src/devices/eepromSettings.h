#include <stdio.h>

typedef struct
{
    uint32_t m_Crc;
    uint8_t Rsv04[8];
    uint8_t m_Region;
    uint8_t m_Rental;
    uint8_t Rsv0F;
    char m_strSerialId[17];
} AM_SYSDATAwH_STATIC, *PAM_SYSDATAwH_STATIC;

typedef struct
{
    // 0 = Common, 1 = Individual
    uint8_t ChuteType;
    // 0 = Common, 1 = Individual
    uint8_t ServiceType;
    // 0 = Normal, 1 = Freeplay
    uint8_t Operation;
    uint8_t CoinRate[2];
    uint8_t BonusAdder;
    uint8_t CreditRate;
    uint8_t Cost[8];
    uint8_t Rsv0F;
} AM_CREDIT_CONFIG, *PAM_CREDIT_CONFIG;

typedef struct
{
    uint32_t m_Crc;
    uint8_t Rsv04[28];
    uint8_t Unk1;
    uint8_t Unk2;
    uint8_t Unk3;
    uint8_t Rsv23[2];
    AM_CREDIT_CONFIG m_Config;
    uint8_t Rsv35[11];
} AM_SYSDATAwH_CREDIT, *PAM_SYSDATAwH_CREDIT;


typedef struct {
    uint32_t m_Flag;
    uint32_t m_IpAddress;
    uint32_t m_SubnetMask;
    uint32_t m_Gateway;
    uint32_t m_PrimaryDns;
    uint32_t m_SecondaryDns;
} AM_SYSDATA_NETWORK_IF, * PAM_SYSDATA_NETWORK_IF;

typedef struct {
    uint32_t m_Crc;
    uint8_t Rsv04[4];
    AM_SYSDATA_NETWORK_IF m_Eth;
} AM_SYSDATAwH_NETWORK, * PAM_SYSDATAwH_NETWORK;

typedef AM_SYSDATAwH_NETWORK AM_SYSDATAwH_NETWORK_ETH0;
typedef AM_SYSDATAwH_NETWORK AM_SYSDATAwH_NETWORK_ETH1;

typedef struct
{
    uint16_t offset;
    uint16_t size;
} EepromOffsets;

#define AM_SYSDATAwH_STATIC_REG 0x000
#define AM_SYSDATAwH_STATIC_DUP 0x200
#define AM_SYSDATAwH_CREDIT_REG 0x100
#define AM_SYSDATAwH_CREDIT_DUP 0x300
#define AM_SYSDATAwH_NETWORK_ETH0_REG 0x0B0
#define AM_SYSDATAwH_NETWORK_ETH0_DUP 0x2B0
#define AM_SYSDATAwH_NETWORK_ETH1_REG 0x0D0
#define AM_SYSDATAwH_NETWORK_ETH1_DUP 0x2D0

int eepromSettingsInit();