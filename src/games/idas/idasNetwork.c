#include <winsock2.h>
#include <string.h>

#include <windows.h>
#include <MinHook.h>

#include "idas.h"
#include "idasNetwork.h"
#include "../elfLoader/symbol.h"
#include "../hooks/network.h"
#include "../config/config.h"
#include "../log/log.h"

static int idas_connect(SOCKET s, const struct sockaddr *name, int namelen);
static int idas_bind(SOCKET s, const struct sockaddr *name, int namelen);
static int idas_send(SOCKET s, const char *buf, int len, int flags);
static int idas_sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen);
static void ensure_source_ip(SOCKET s, const SOCKADDR_IN *dst);
static const netFunc idas_net_func = {
    .connect            = idas_connect,
    .bind               = idas_bind,
    .send               = idas_send,
    .sendto             = idas_sendto,
};

static void overrideSubnet(uintptr_t addr)
{
    if(!addr) return;
    uint8_t subnet[17];
    int length = strlen(Config.network.subnet);
    if (strstr(Config.network.subnet, "0.0.0.0") != NULL)
        return;
    memcpy(subnet, Config.network.subnet, length);
    if (length < 16)
        memset(subnet + length, 0, 16 - length);
    
    for (int i = 0; i < 4; i++)
    {
        patchMemory((void*)addr, subnet + i * 4, 4);
        addr += 11;
    }   
}

/* ===================================================================== */
/* Network function hooks for IDAS to redirect ServerBox network traffic */
/* ===================================================================== */
static int idas_connect(SOCKET s, const struct sockaddr *name, int namelen)
{
    ensure_source_ip(s, (PSOCKADDR_IN)name);

    uint32_t addr = _byteswap_ulong(((SOCKADDR_IN*)name)->sin_addr.S_un.S_addr);
    USHORT port = _byteswap_ushort(((SOCKADDR_IN*)name)->sin_port);

    if (isServerBox() && Config.network.simulateNetwork)
    {
        uint32_t subnet = getNetSubnetAddr();
        uint32_t net_mask = ntohl(getNetHostMask());
        if ((addr & net_mask) == subnet) 
        {
            uint32_t hostaddr = ntohl(getNetHostAddr());
            SOCKADDR_IN addr_in;
            memcpy(&addr_in, name, sizeof(SOCKADDR_IN));
            
            uint32_t target = (subnet & net_mask) | (addr & ~net_mask);
            if (target == hostaddr) 
            {
                USHORT new_port = port + 1;
                addr_in.sin_addr.s_addr = htonl(target);
                addr_in.sin_port = htons(new_port);

                log_debug(plfNetwork,
                    "[TCP] redirect %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u",
                    addr >> 24,
                    (addr >> 16) & 0xFF,
                    (addr >> 8) & 0xFF,
                    addr & 0xFF,
                    port,
                    subnet >> 24,
                    (subnet >> 16) & 0xFF,
                    (subnet >> 8) & 0xFF,
                    addr & 0xFF,
                    new_port
                );
            }
            return connect(s, (SOCKADDR*)&addr_in, sizeof(addr_in));
        }
    }
    
    return connect(s, name, namelen);
    
}

static bool serverbox_bind_ip_ready = false;
static uint32_t serverbox_bind_ip = 0;  // network byte order; 0 = disabled

static uint32_t get_serverbox_bind_ip(void)
{
    if (!serverbox_bind_ip_ready) {
        serverbox_bind_ip_ready = true;
        const char *bind_ip = Config.network.bind_ip;
        if (bind_ip != NULL && bind_ip[0] != '\0') {
            serverbox_bind_ip = inet_addr(bind_ip);
            if (serverbox_bind_ip == INADDR_NONE || serverbox_bind_ip == INADDR_ANY) {
                log_error(plfNetwork,
                          "Invalid network.bind_ip %s, dedicated-IP mode disabled", bind_ip);
                serverbox_bind_ip = 0;
            }
        }
    }
    return serverbox_bind_ip;
}

static bool game_ip_ready = false;
static uint32_t game_ip = 0;  // network byte order; 0 = disabled

static uint32_t get_game_ip(void)
{
    if (!game_ip_ready) {
        game_ip_ready = true;
        const char *ip = Config.network.game_ip;
        if (ip != NULL && ip[0] != '\0') {
            game_ip = inet_addr(ip);
            if (game_ip == INADDR_NONE || game_ip == INADDR_ANY) {
                log_error(plfNetwork, "Invalid network.game_ip %s, ignoring", ip);
                game_ip = 0;
            }
        }
    }
    return game_ip;
}

static void ensure_source_ip(SOCKET s, const SOCKADDR_IN *dst)
{
    if (dst != NULL && (ntohl(dst->sin_addr.s_addr) >> 24) == 127)
        return;  // loopback destination: source IP is irrelevant

    uint32_t src = isServerBox() ? get_serverbox_bind_ip() : get_game_ip();
    if (src == 0)
        return;

    SOCKADDR_IN local;
    int len = sizeof(local);
    if (getsockname(s, (SOCKADDR*)&local, &len) != 0 || local.sin_family != AF_INET)
        return;
    if (local.sin_addr.S_un.S_addr != INADDR_ANY || local.sin_port != 0)
        return;  // already carries an explicit bind (or is connected)

    SOCKADDR_IN bind_addr = {0};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = src;  // port 0: OS picks the local port
    if (bind(s, (SOCKADDR*)&bind_addr, sizeof(bind_addr)) == SOCKET_ERROR) {
        uint32_t n = ntohl(src);
        log_debug(plfNetwork, "source pin to %hhu.%hhu.%hhu.%hhu failed: %d",
                  (n >> 24) & 0xff, (n >> 16) & 0xff, (n >> 8) & 0xff, n & 0xff,
                  WSAGetLastError());
    }
}

static int idas_bind(SOCKET s, const struct sockaddr *name, int namelen)
{
    ULONG addr = _byteswap_ulong(((SOCKADDR_IN*)name)->sin_addr.S_un.S_addr);
    USHORT port = _byteswap_ushort(((SOCKADDR_IN*)name)->sin_port);
    if (name->sa_family == AF_INET && (get_serverbox_bind_ip() != 0 || get_game_ip() != 0)) {
        // Game and ServerBox listeners share the port on different local IPs;
        // unicast to each address is delivered to its own socket only.
        BOOL reuse = TRUE;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

        uint32_t role_ip = isServerBox() ? get_serverbox_bind_ip() : get_game_ip();
        if (role_ip != 0 && ((SOCKADDR_IN*)name)->sin_addr.S_un.S_addr == INADDR_ANY) {
            SOCKADDR_IN addr_in = *(const SOCKADDR_IN*)name;
            addr_in.sin_addr.s_addr = role_ip;

            ULONG new_addr = ntohl(role_ip);
            log_debug(plfNetwork, "bind(%hhu.%hhu.%hhu.%hhu:%hu -> %hhu.%hhu.%hhu.%hhu:%hu)",
                     (addr >> 24) & 0xff, (addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff,
                     port, (new_addr >> 24) & 0xff, (new_addr >> 16) & 0xff,
                     (new_addr >> 8) & 0xff, new_addr & 0xff, port);
            return bind(s, (SOCKADDR*)&addr_in, sizeof(addr_in));
        }
        // Explicit (non-wildcard) address or no IP for this role: unchanged.
    }
    if (isServerBox() && port != 0 && port != 65535 && Config.network.simulateNetwork)
    {
        SOCKADDR_IN addr_in = *(const SOCKADDR_IN*)name;
        uint16_t new_port = ntohs(addr_in.sin_port);
        new_port += 1;
        addr_in.sin_port = htons(new_port);

        log_debug(plfNetwork, "bind(%hhu.%hhu.%hhu.%hhu:%hu -> %hu)", (addr >> 24) & 0xff, (addr >> 16) & 0xff,
            (addr >> 8) & 0xff, addr & 0xff, port, new_port);
        return bind(s, (SOCKADDR*)&addr_in, sizeof(addr_in));
    }
    log_debug(plfNetwork, "bind(%hhu.%hhu.%hhu.%hhu:%hu)", (addr >> 24) & 0xff, (addr >> 16) & 0xff,
             (addr >> 8) & 0xff, addr & 0xff, port);
    return bind(s, name, namelen);
}

static int idas_send(SOCKET s, const char *buf, int len, int flags)
{
    ensure_source_ip(s, NULL);
    return send(s, buf, len, flags);
}

static int idas_sendto(SOCKET s,const char *buf,int len,int flags,const struct sockaddr *to,int tolen)
{
    static SOCKADDR pingSentTo;
    static unsigned char pingInfo[4];
    static SOCKADDR_IN toLocalhost = {
        .sin_addr.S_un.S_addr = 0x0100007f,
        .sin_family = AF_INET,
        .sin_port = 24,
        .sin_zero = {0},
    };

    PSOCKADDR_IN in = (PSOCKADDR_IN)to;

    uint32_t addr = ntohl(((PSOCKADDR_IN)to)->sin_addr.s_addr);
    uint16_t port_host = ntohs(in->sin_port);
    if (len == 8 && buf[0] == 0x08 && buf[1] == 0x00) {
        memcpy(&pingSentTo, to, sizeof pingSentTo);

        uint16_t seq = _byteswap_ushort(((uint16_t*)buf)[3]);

        memcpy(pingInfo, buf + 4, 4);

        memcpy(&toLocalhost, to, sizeof toLocalhost);
        toLocalhost.sin_addr.S_un.S_addr = 0x0100007f;  // 127.0.0.1

        log_debug(plfNetwork, "(probable) Ping to: %d.%d.%d.%d (%d). Redirecting to localhost",
            addr >> 24, (addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff,
            ((PSOCKADDR_IN)to)->sin_port, seq);

        return sendto(s, buf, len, flags, (PSOCKADDR)&toLocalhost, tolen);
    }

    ensure_source_ip(s, in);

    if (!isServerBox() && get_serverbox_bind_ip() != 0 && in->sin_family == AF_INET) {
        uint32_t net_mask = ntohl(getNetHostMask());
        if ((addr & ~net_mask) == (~net_mask)) {  // limited or subnet-directed broadcast
            SOCKADDR_IN serverbox = *in;
            serverbox.sin_addr.s_addr = get_serverbox_bind_ip();

            uint32_t sb_addr = ntohl(get_serverbox_bind_ip());
            log_debug(plfNetwork,
                     "[UDP] broadcast %u.%u.%u.%u:%u -> serverbox %hhu.%hhu.%hhu.%hhu:%u",
                     (addr >> 24) & 0xFF, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF,
                     port_host, (sb_addr >> 24) & 0xff, (sb_addr >> 16) & 0xff,
                     (sb_addr >> 8) & 0xff, sb_addr & 0xff, port_host);

            int ret = sendto(s, buf, len, flags, (PSOCKADDR)&serverbox, sizeof(serverbox));
            if (ret == SOCKET_ERROR)
                log_error(plfNetwork,
                          "[UDP] redirect to serverbox %hhu.%hhu.%hhu.%hhu:%u failed: %d",
                          (sb_addr >> 24) & 0xff, (sb_addr >> 16) & 0xff,
                          (sb_addr >> 8) & 0xff, sb_addr & 0xff, port_host, WSAGetLastError());
            return ret;
        }
    }

    if (!Config.network.simulateNetwork)
        return sendto(s, buf, len, flags, to, tolen);
        
    uint32_t subnet = getNetSubnetAddr();
    uint32_t net_host_addr = ntohl(getNetHostAddr());
    uint32_t net_mask = ntohl(getNetHostMask());

    if ((addr & ~net_mask) == (~net_mask) || (addr == net_host_addr)) {
        SOCKADDR_IN new_to = *in;

        if ((addr & ~net_mask) == (~net_mask)) // Boardcast
            new_to.sin_addr.s_addr = htonl((subnet | (~net_mask))); // Redirect to subnet
        else // Unicast
            new_to.sin_addr.s_addr = htonl((subnet & net_mask) | (addr & ~net_mask));

        log_debug(plfNetwork,
            "[UDP] redirect host: %u.%u.%u.%u -> %u.%u.%u.%u", addr >> 24, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF,
            subnet >> 24, (subnet >> 16) & 0xFF, (subnet >> 8) & 0xFF, addr & 0xFF);

        if (!isServerBox())
        {
            sendto(s, buf, len, flags, (SOCKADDR*)&new_to, sizeof(new_to)); // Send to the client boot with the serverbox.
            new_to.sin_port = htons(port_host + 1);

            log_debug(plfNetwork,
                "[UDP] redirect port: %u -> %u", port_host, port_host + 1);
        }
        return sendto(s, buf, len, flags, (SOCKADDR*)&new_to, sizeof(new_to));
    }
    return sendto(s, buf, len, flags, to, tolen); 
}

bool init_idas_network(const struct _IDAS_InitialData *init)
{
    bool serverBox = false;

    InitNetworkBridge((netFunc *)&idas_net_func);
    if (init)
    {
        serverBox = strstr(init->verName, "Server Box") != NULL;
        setServerBox(serverBox);

        // Network patches
        patchMachineCode(init->networkPatch.SkipInitialization.addr,
            !Config.network.enable ? init->networkPatch.SkipInitialization.newStr2 :
            Config.sysconf.skipInitCard ? init->networkPatch.SkipInitialization.newStr : init->networkPatch.SkipInitialization.newStr3);
        patchMachineCode(init->networkPatch.KEYCHECK.addr, init->networkPatch.KEYCHECK.newStr); // for D5 EXP
    }

    if (Config.network.enable)
        dns_entry_init();

    // Network patches
    setVariable((size_t)(uintptr_t)get_variable_symbol_ptr("_ZN8DEMOMODE7NETWORKE"),
                (size_t)(uintptr_t)Config.network.subnet);
    
    for(int i = 0; i < 2; i++)
        overrideSubnet(init->networkPatch.InstoreSubnet[i]);

    return serverBox;
}
