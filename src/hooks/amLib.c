#include "amLib.h"
#include "../elfLoader/symbol.h"
#include "../devices/securityBoard.h"
#include "../games/patchlib.h"
#include "../log/log.h"
#include "../config/config.h"
#include <winsock2.h>
#include <minwindef.h>
#include <MinHook.h>

static m_Boot s_arcadeContext;

void setupGameId(const char* _pcGameId)
{
    memcpy(s_arcadeContext.m_GameId, _pcGameId, 4);
}

int amDongleUserInfoEx(int a, int b, char* _arcadeContext)
{
    m_Boot* bp = (m_Boot*)_arcadeContext;
    if (*(int*)s_arcadeContext.m_GameId != 0)
        memcpy(bp->m_GameId, s_arcadeContext.m_GameId, 4);
    else
        if(Config.sysconf.game_id[0] != 0)
            memcpy(bp->m_GameId, Config.sysconf.game_id, 4);
    return 0;
}

int amDipswGetData(uint8_t* dip)
{
    uint8_t result;
    uint32_t data;

    securityBoardIn(0x38, &data);

    result = 0x00;
    if ((~data & 0x10) != 0)
        result |= 0x01; // Dip 7
    if ((~data & 0x20) != 0)
        result |= 0x02; // Dip 8
    if ((~data & 0x40) != 0)
        result |= 0x04; // Dip 1
    if ((~data & 0x80) != 0)
        result |= 0x08; // Dip 2
    if ((~data & 0x100) != 0)
        result |= 0x10; // Rotation Dip 3
    if ((~data & 0x200) != 0)
        result |= 0x20; // Resolution Dip 4
    if ((~data & 0x400) != 0)
        result |= 0x40; // Resolution Dip 5
    if ((~data & 0x800) != 0)
        result |= 0x80; // Resolution Dip 6
    *dip = result;
    return 0;
}

int amOsinfoGetNetworkProperty(void* out)
{
    if (!Config.network.enable)
        return -2;

    AM_SYSDATAwH_NETWORK* np = (AM_SYSDATAwH_NETWORK*)out;

    np->m_Flag = Config.sysconf.enableDHCP ? 1 : 0; // DHCP enabled
    np->m_IpAddress = Config.sysconf.ip_address;
    np->m_SubnetMask = Config.sysconf.subnet_mask;
    np->m_Gateway = Config.sysconf.gateway; // 192.168.37.254
    np->m_PrimaryDns = Config.sysconf.primary_dns; // 192.168.37.254
    np->m_SecondaryDns = Config.sysconf.secondary_dns; // 192.168.37.254

    return 0;
}

int InitAmLibHooks() {
    log_info(plfBoot, "Initializing AmLibs...");
    // Debug Variables
    if (Config.sysconf.log_level <= LOG_DEBUG)
    {
        setVariable((size_t)(uintptr_t)get_variable_symbol_ptr("amBackupDebugLevel"), 2);
        setVariable((size_t)(uintptr_t)get_variable_symbol_ptr("amCreditDebugLevel"), 2);
        setVariable((size_t)(uintptr_t)get_variable_symbol_ptr("amDipswDebugLevel"), 2);
        setVariable((size_t)(uintptr_t)get_variable_symbol_ptr("amDongleDebugLevel"), 2);
        setVariable((size_t)(uintptr_t)get_variable_symbol_ptr("amEepromDebugLevel"), 2);
        setVariable((size_t)(uintptr_t)get_variable_symbol_ptr("amHwmonitorDebugLevel"), 2);
        setVariable((size_t)(uintptr_t)get_variable_symbol_ptr("amJvsDebugLevel"), 2);
        setVariable((size_t)(uintptr_t)get_variable_symbol_ptr("amLibDebugLevel"), 2);
        setVariable((size_t)(uintptr_t)get_variable_symbol_ptr("amMiscDebugLevel"), 2);
        setVariable((size_t)(uintptr_t)get_variable_symbol_ptr("amSysDataDebugLevel"), 2);
        setVariable((size_t)(uintptr_t)get_variable_symbol_ptr("amOsinfoDebugLevel"), 2);
        setVariable((size_t)(uintptr_t)get_variable_symbol_ptr("bcLibDebugLevel"), 2);
        setVariable((size_t)(uintptr_t)get_variable_symbol_ptr("s_logMask"), 0xFFFFFFFF);
    }

    // Dongle patches
    MH_CreateHook((LPVOID)get_native_symbol_ptr("amDongleInit"), stubRetZero, NULL);
    MH_CreateHook((LPVOID)get_native_symbol_ptr("amDongleIsAvailable"), stubRetOne, NULL);
    MH_CreateHook((LPVOID)get_native_symbol_ptr("amDongleUpdate"), stubRetZero, NULL);
    MH_CreateHook((LPVOID)get_native_symbol_ptr("amDongleUserInfoEx"), amDongleUserInfoEx, NULL); // Read Info
    MH_CreateHook((LPVOID)get_native_symbol_ptr("amDongleDecryptEx"), stubRetZero, NULL);


    // DipSW patches
    MH_CreateHook((LPVOID)get_native_symbol_ptr("amDipswInit"), stubRetZero, NULL);
    MH_CreateHook((LPVOID)get_native_symbol_ptr("amDipswExit"), stubRetZero, NULL);
    MH_CreateHook((LPVOID)get_native_symbol_ptr("amDipswGetData"), amDipswGetData, NULL);
    MH_CreateHook((LPVOID)get_native_symbol_ptr("amDipswSetLed"), stubRetZero, NULL);

    // amOs
    MH_CreateHook((LPVOID)get_native_symbol_ptr("amOsinfoGetNetworkProperty"), amOsinfoGetNetworkProperty, NULL); // Need to be changed on demand for EXP
    MH_CreateHook((LPVOID)get_native_symbol_ptr("amOsinfoExecDhcpNic"), stubRetZero, NULL);
    MH_CreateHook((LPVOID)get_native_symbol_ptr("amsInit"), stubRetZero, NULL);

    return 0;
}