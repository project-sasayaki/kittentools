#include <stdint.h>
#include <string.h>
#include <winsock2.h>
#include "eepromSettings.h"
#include "../config/config.h"
#include "../log/log.h"
#include "../utils/Crc.h"

#define fix_crc(block)                                                              \
    do {                                                                            \
        (block).m_Crc = amiCrc32RCalc(sizeof(block) - 4, (uint8_t*)(&(block)) + 4); \
    } while (0)

unsigned char eepromBuffer[1024];

int createStaticSection()
{
    AM_SYSDATAwH_STATIC Static = {
        .m_Region = Config.sysconf.region & (1 | 2 | 4 | 8),
        .m_Rental = 0,
    };
    ZeroMemory(Static.m_strSerialId, sizeof Static.m_strSerialId);
    DWORD len = strlen(Config.sysconf.keychip_serial);
    if (len > sizeof Static.m_strSerialId - 1)
        len = sizeof Static.m_strSerialId - 1;
    memcpy(Static.m_strSerialId, Config.sysconf.keychip_serial, len);
    fix_crc(Static);
    memcpy(&eepromBuffer[AM_SYSDATAwH_STATIC_REG], &Static, sizeof Static);
    memcpy(&eepromBuffer[AM_SYSDATAwH_STATIC_DUP], &Static, sizeof Static);
    return 0;
}

int createEthSection()
{

    AM_SYSDATAwH_NETWORK_ETH0 NetEth0 = { 0 };

    NetEth0.m_Eth.m_Flag = 1;  // TODO: Allow DHCP
    NetEth0.m_Eth.m_IpAddress = inet_addr("10.0.0.1");
    NetEth0.m_Eth.m_SubnetMask = inet_addr("255.255.255.0");
    NetEth0.m_Eth.m_Gateway = inet_addr("0.0.0.0");
    NetEth0.m_Eth.m_PrimaryDns = inet_addr("0.0.0.0");
    NetEth0.m_Eth.m_SecondaryDns = inet_addr("0.0.0.0");

    fix_crc(NetEth0);
    memcpy(&eepromBuffer[AM_SYSDATAwH_NETWORK_ETH0_REG], &NetEth0, sizeof NetEth0);
    memcpy(&eepromBuffer[AM_SYSDATAwH_NETWORK_ETH0_DUP], &NetEth0, sizeof NetEth0);

    // TODO: Does anything ever use ETH1?
    AM_SYSDATAwH_NETWORK_ETH1 NetEth1 = { 0 };
    fix_crc(NetEth1);
    memcpy(&eepromBuffer[AM_SYSDATAwH_NETWORK_ETH1_REG], &NetEth1, sizeof NetEth1);
    memcpy(&eepromBuffer[AM_SYSDATAwH_NETWORK_ETH1_DUP], &NetEth1, sizeof NetEth1);
    return 0;
}

int createCreditSection()
{
    AM_SYSDATAwH_CREDIT Credit = {
        .Unk1 = 9,
        .Unk2 = 9,
        .Unk3 = 1,
        .m_Config = {
            .ChuteType = 0,
            .ServiceType = 1,
            .Operation = Config.Coins.freeplay,
            .CoinRate = {1, 1},
            .BonusAdder = 0,
            .CreditRate = Config.Coins.credit_rate,
            .Cost = {
                Config.Coins.start, 
                Config.Coins.continues,
                Config.Coins.card, 
                Config.Coins.car,
                1, // D.Net Subscribe
                1, // D.Coin Level 1
                1, // D.Coin Level 2
                1  // D.Coin Level 3
            }
        },
    };

    fix_crc(Credit);
    memcpy(&eepromBuffer[AM_SYSDATAwH_CREDIT_REG], &Credit, sizeof Credit);
    memcpy(&eepromBuffer[AM_SYSDATAwH_CREDIT_DUP], &Credit, sizeof Credit);
    return 0;
}

/* Clobber EEPROM with our user settings */
void eepromFixup() {
    createStaticSection();
    createEthSection();
    createCreditSection();
}

int writeSectiontoFile(FILE* file)
{
    return fwrite(eepromBuffer, sizeof(eepromBuffer), 1, file);
}

int eepromSettingsInit()
{
    FILE* eeprom = fopen(Config.path.eeprom_path, "r+b");
    if (eeprom == NULL)
    {
        log_error(plfEeprom, "EEPROM file not found, building default EEPROM file.");
        eeprom = fopen(Config.path.eeprom_path, "w+b");
        if (eeprom == NULL)
        {
            log_error(plfEeprom, "Cannot create EEPROM file.");
            return -1;
        }
    }
    eepromFixup();
    fwrite(eepromBuffer, sizeof(eepromBuffer), 1, eeprom);
    fclose(eeprom);
    return 0;
}
