#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../subprojects/inih_dep/ini.h"
#include "../log/log.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>


static uint32_t net_host_addr = 0;
static uint32_t net_mask = 0;
static uint32_t net_subnet_addr = 0;
static bool is_server_box = false;

config_t Config = {
#define SECTION(s, comment) .s = {
#define CFG_str(s, n, default, comment) .n = default,
#define CFG_bool(s, n, default, comment) .n = default,
#define CFG_int(s, n, default, comment) .n = default,
#define CFG_hex(s, n, precision, default, comment) .n = default,
#define CFG_ipv4(s, n, a, b, c, d, comment)                                   \
    .n = (unsigned int)(((unsigned char)d << 24) | ((unsigned char)c << 16) | \
                        ((unsigned char)b << 8) | (unsigned char)a),
#define ENDSECTION(s) \
    }                 \
    ,
#include "config.def"
    ._keep_linter_happy = true
};

static void fprintf_prefix(FILE *file, const char *prefix, const char *text) {
    char *copy = (char *)malloc(strlen(text) + 1);
    memcpy_s(copy, strlen(text) + 1, text, strlen(text) + 1);

    char *next_token;
    char *token = strtok_s(copy, "\n", &next_token);
    while (token != NULL) {
        fprintf(file, "%s%s\n", prefix, token);
        token = strtok_s(NULL, "\n", &next_token);
    }

    free(copy);
}

void save_current_config(bool writeDefault) {
    CreateDirectoryA("kitten", NULL);

    FILE *config_file;
    fopen_s(&config_file, CONFIG_PATH, "w");
    if (config_file == NULL) {
        puts("Failed to create config file!");
        return;
    }
    int first_section = true;

#define CFG_str(s, n, default, comment)                                                         \
    if (strlen(comment) != 0) fprintf_prefix(config_file, "; ", comment);                       \
    fprintf(config_file, "; (string) default = %s\n", writeDefault ? default : Config.s.n); \
    fprintf(config_file, "%s = %s\n", #n, Config.s.n);

#define CFG_bool(s, n, default, comment)                                   \
    if (strlen(comment) != 0) fprintf_prefix(config_file, "; ", comment);  \
    fprintf(config_file, "; (bool) default = %s\n",                        \
            (writeDefault ? default : Config.s.n) ? "true" : "false"); \
    fprintf(config_file, "%s = %s\n", #n, Config.s.n ? "true" : "false");

#define CFG_int(s, n, default, comment)                                                      \
    if (strlen(comment) != 0) fprintf_prefix(config_file, "; ", comment);                    \
    fprintf(config_file, "; (int) default = %d\n", writeDefault ? default : Config.s.n); \
    fprintf(config_file, "%s = %d\n", #n, Config.s.n);

#define CFG_hex(s, n, precision, default, comment)                                              \
    if (strlen(comment) != 0) fprintf_prefix(config_file, "; ", comment);                       \
    fprintf(config_file, "; (hex, %d byte%s) default = 0x%.*X \n", precision,                     \
            precision == 1 ? "" : "s", precision, writeDefault ? default : Config.s.n); \
    fprintf(config_file, "%s = 0x%.*X\n", #n, precision, Config.s.n);

#define CFG_ipv4(s, n, a, b, c, d, comment)                                        \
    if (strlen(comment) != 0) fprintf_prefix(config_file, "; ", comment);          \
    fprintf(config_file, "; (ipv4) default = %hhu.%hhu.%hhu.%hhu ;(ipv4)\n",       \
            writeDefault ? a: (Config.s.n & 0xff),                           \
            writeDefault ? b: (Config.s.n >> 8) & 0xff,                      \
            writeDefault ? c: (Config.s.n >> 16) & 0xff,                     \
            writeDefault ? d: (Config.s.n >> 24));                           \
    fprintf(config_file, "%s = %hhu.%hhu.%hhu.%hhu\n", #n, (Config.s.n & 0xff), \
            ((Config.s.n >> 8) & 0xff), ((Config.s.n >> 16) & 0xff),       \
            (Config.s.n >> 24));

#define SECTION(s, comment)                                               \
    if (!first_section) fprintf(config_file, "\n");                       \
    first_section = false;                                                \
    if (strlen(comment) != 0) fprintf_prefix(config_file, "; ", comment); \
    fprintf(config_file, "[%s]\n", #s);

#define COMMENT(comment) \
    if (strlen(comment) != 0) fprintf_prefix(config_file, "; ", comment);

#define NEWLINE() fprintf(config_file, "\n");

#include "config.def"

    fclose(config_file);
}

static int handler(void *user, const char *section, const char *name, const char *value) {
    config_t *cfg = (config_t *)user;

    char *end;

    if (false)
        ;
#define CFG_str(s, n, default, comment) \
    else if (_stricmp(section, #s) == 0 && _stricmp(name, #n) == 0) cfg->s.n = _strdup(value);
#define CFG_bool(s, n, default, comment)                                       \
    else if (_stricmp(section, #s) == 0 && _stricmp(name, #n) == 0) cfg->s.n = \
        strcmp(value, "true") == 0;
#define CFG_int(s, n, default, comment)                                          \
    else if (_stricmp(section, #s) == 0 && _stricmp(name, #n) == 0) {            \
        cfg->s.n = strtol(value, &end, 10);                                      \
        if (end == value || *end != '\0' || errno == ERANGE) cfg->s.n = default; \
    }
#define CFG_hex(s, n, precision, default, comment)                                   \
    else if (_stricmp(section, #s) == 0 && _stricmp(name, #n) == 0) {                \
        cfg->s.n = strtol(value, &end, 16);                                          \
        if (end == value || *end != '\0' || errno == ERANGE) cfg->s.n = default; \
    }
#define CFG_ipv4(s, n, a, b, c, d, comment)                                                 \
    else if (_stricmp(section, #s) == 0 && _stricmp(name, #n) == 0) {                       \
        unsigned char ip_a, ip_b, ip_c, ip_d;                                               \
        if (sscanf(value, "%hhu.%hhu.%hhu.%hhu", &ip_a, &ip_b, &ip_c, &ip_d) == 4)          \
            cfg->s.n = (unsigned int)((ip_d << 24) | (ip_c << 16) | (ip_b << 8) | ip_a);    \
        else                                                                                \
            cfg->s.n = (unsigned int)(((unsigned char)d << 24) | ((unsigned char)c << 16) | \
                                      ((unsigned char)b << 8) | (unsigned char)a);          \
    }

#include "config.def"

    return 1;
}

static int parseSubnetConfig() {
    const char *subnet_str = Config.network.subnet;
    
    char subnet_addr[32] = {0};
    int cidr = 24;
    
    char *slash = strchr(subnet_str, '/');
    if (slash != NULL) {
        size_t addr_len = slash - subnet_str;
        strncpy(subnet_addr, subnet_str, addr_len);
        subnet_addr[addr_len] = '\0';
        cidr = atoi(slash + 1);
    } else {
        strncpy(subnet_addr, subnet_str, sizeof(subnet_addr) - 1);
    }
    
    uint32_t mask = (cidr == 0) ? 0 : (0xFFFFFFFF << (32 - cidr));
    net_mask = htonl(mask);
    
    struct in_addr subnet_in;
    uint32_t subnet_network = 0;

    if (InetPtonA(AF_INET, subnet_addr, &subnet_in) == 1) {
        subnet_network = ntohl(subnet_in.s_addr) & mask;
    }
    
    net_subnet_addr = subnet_network;
    net_host_addr = 0;
    
    PIP_ADAPTER_ADDRESSES pAddresses = NULL;
    ULONG outBufLen = 0;

    GetAdaptersAddresses(AF_INET, 0, NULL, pAddresses, &outBufLen);

    pAddresses = (PIP_ADAPTER_ADDRESSES)malloc(outBufLen);
    if (pAddresses == NULL) {
        log_error(plfBoot, "Failed to allocate memory for adapter addresses");
        return 3;
    }

    if (GetAdaptersAddresses(AF_INET, 0, NULL, pAddresses, &outBufLen)) {
        log_error(plfBoot, "GetAdaptersAddresses failed");
        free(pAddresses);
        return 2;
    }

    PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses;
    while (pCurrAddresses != NULL) {
        PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurrAddresses->FirstUnicastAddress;
        while (pUnicast != NULL) {
            if (pUnicast->Address.lpSockaddr->sa_family == AF_INET) {
                struct sockaddr_in *addr = (struct sockaddr_in *)pUnicast->Address.lpSockaddr;
                uint32_t ip_addr = ntohl(addr->sin_addr.s_addr);

                if ((ip_addr & mask) == subnet_network) {
                    net_host_addr = addr->sin_addr.s_addr;
                    break;
                }
            }
            pUnicast = pUnicast->Next;
        }
        if (net_host_addr != 0) {
            break;
        }
        pCurrAddresses = pCurrAddresses->Next;
    }
    
    free(pAddresses);
    
    if (net_host_addr == 0) {
        log_error(plfBoot, "No network interface found in subnet %s", subnet_str);
        return 1;
    }

    return 0;
}

int getNetHostAddr() {
    return net_host_addr;
}

int getNetSubnetAddr() {
    return net_subnet_addr;
}

int getNetHostMask() {
    return net_mask;
}

void setServerBox(bool flag) {
    is_server_box = flag;
}

bool isServerBox() {
    return is_server_box;
}

const unsigned int RES_W[8] = {640, 800, 1024, 1280, 800, 1024, 1280, 1360};
const unsigned int RES_H[8] = {480, 480, 768, 768, 600, 600, 1024, 768};
void load_config() {
    if (ini_parse(CONFIG_PATH, handler, &Config) < 0)
        log_error(plfBoot, "Configuration file not found at \"%s\", loading default settings", CONFIG_PATH);

    save_current_config(true);

    if (parseSubnetConfig())
    {
        log_warn(plfNetwork, "Network disabled");
        Config.network.enable = false;
    }

    if (Config.display.dipsw)
    {
        Config.display.width = RES_W[(Config.sysconf.dipsw >> 3) & 0b111];
        Config.display.height = RES_H[(Config.sysconf.dipsw >> 3) & 0b111];
    }
    else
    {
        Config.sysconf.dipsw &= 0b11'000'111;

        if (Config.display.width == 1360 && Config.display.height == 768)
            Config.sysconf.dipsw |= DIPSW_RES_1360x768;
        else if (Config.display.width == 1280 && Config.display.height == 1024)
            Config.sysconf.dipsw |= DIPSW_RES_1280x1024;
        else if (Config.display.width == 1024 && Config.display.height == 600)
            Config.sysconf.dipsw |= DIPSW_RES_1024x600;
        else if (Config.display.width == 800 && Config.display.height == 600)
            Config.sysconf.dipsw |= DIPSW_RES_800x600;
        else if (Config.display.width == 1280 && Config.display.height == 768)
            Config.sysconf.dipsw |= DIPSW_RES_1280x768;
        else if (Config.display.width == 1024 && Config.display.height == 768)
            Config.sysconf.dipsw |= DIPSW_RES_1024x768;
        else if (Config.display.width == 800 && Config.display.height == 480)
            Config.sysconf.dipsw |= DIPSW_RES_800x480;
        else
            Config.sysconf.dipsw |= DIPSW_RES_DEFAULT;
    }
}