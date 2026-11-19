#include "config.h"
#include <errno.h>
#include <stddef.h>
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
#define SECTION(s, text) .s = {
#define ENDSECTION(s) \
    }                 \
    ,
#include "configDefDefaults.h"
#include "config.def"
    ._keep_linter_happy = true
};

static const ConfigItem g_items[] = {
#define CONFIG_DEF_OFFSET(s, n) offsetof(config_t, s.n)
#include "configDefItems.h"
#include "config.def"
#undef CONFIG_DEF_OFFSET
};

static const ConfigTable g_table = {
    .items = g_items,
    .count = sizeof(g_items) / sizeof(g_items[0]),
    .values = &Config,
};

/* The settings of config.def as the file gave them. load_config() goes on to derive some of Config
 * from the rest (the resolution from the DIP switches, the network off without an adapter) and a
 * game may set more of it; the file keeps what the player wrote. */
static config_t g_fileConfig;
static bool g_fileConfigRead = false;

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

static void write_comment(FILE *file, const char *comment) {
    if (comment != NULL && strlen(comment) != 0) fprintf_prefix(file, "; ", comment);
}

/* The items of the groups given, and those that stand before the table's first GROUP(). */
static void write_table(FILE *file, const ConfigTable *table, const void *values, unsigned groups,
                        bool *first_section) {
    bool wanted = true;

    for (size_t i = 0; i < table->count; i++) {
        const ConfigItem *item = &table->items[i];
        const void *value = (const char *)values + item->offset;

        if (item->type == CONFIG_ITEM_GROUP) {
            wanted = (item->group & groups) != 0;
            continue;
        }
        if (!wanted) continue;

        switch (item->type) {
            case CONFIG_ITEM_SECTION:
                if (!*first_section) fprintf(file, "\n");
                *first_section = false;
                write_comment(file, item->comment);
                fprintf(file, "[%s]\n", item->section);
                break;
            case CONFIG_ITEM_COMMENT:
                write_comment(file, item->comment);
                break;
            case CONFIG_ITEM_NEWLINE:
                fprintf(file, "\n");
                break;
            case CONFIG_ITEM_STR:
                write_comment(file, item->comment);
                fprintf(file, "; (string) default = %s\n", item->defaultStr);
                fprintf(file, "%s = %s\n", item->name, *(char *const *)value);
                break;
            case CONFIG_ITEM_BOOL:
                write_comment(file, item->comment);
                fprintf(file, "; (bool) default = %s\n", item->defaultInt ? "true" : "false");
                fprintf(file, "%s = %s\n", item->name, *(const bool *)value ? "true" : "false");
                break;
            case CONFIG_ITEM_INT:
                write_comment(file, item->comment);
                fprintf(file, "; (int) default = %d\n", item->defaultInt);
                fprintf(file, "%s = %d\n", item->name, *(const int *)value);
                break;
            case CONFIG_ITEM_HEX:
                write_comment(file, item->comment);
                fprintf(file, "; (hex, %d byte%s) default = 0x%.*X \n", item->precision,
                        item->precision == 1 ? "" : "s", item->precision, item->defaultInt);
                fprintf(file, "%s = 0x%.*X\n", item->name, item->precision, *(const int *)value);
                break;
            case CONFIG_ITEM_IPV4: {
                unsigned int d = item->defaultIpv4;
                unsigned int v = *(const unsigned int *)value;
                write_comment(file, item->comment);
                fprintf(file, "; (ipv4) default = %u.%u.%u.%u ;(ipv4)\n", d & 0xff, (d >> 8) & 0xff,
                        (d >> 16) & 0xff, d >> 24);
                fprintf(file, "%s = %u.%u.%u.%u\n", item->name, v & 0xff, (v >> 8) & 0xff,
                        (v >> 16) & 0xff, v >> 24);
                break;
            }
            default:
                break;
        }
    }
}

void config_write(unsigned groups, const ConfigSelection *own, size_t ownCount) {
    CreateDirectoryA("kitten", NULL);

    FILE *config_file;
    fopen_s(&config_file, CONFIG_PATH, "w");
    if (config_file == NULL) {
        puts("Failed to create config file!");
        return;
    }
    bool first_section = true;

    write_table(config_file, &g_table, g_fileConfigRead ? &g_fileConfig : &Config,
                groups | CONFIG_GROUP_COMMON, &first_section);
    for (size_t i = 0; i < ownCount; i++) {
        if (own[i].table != NULL)
            write_table(config_file, own[i].table, own[i].table->values, own[i].groups, &first_section);
    }

    fclose(config_file);
}

/* inih handler: one "name = value" line of the file, given to the table's setting of that name. */
static int handler(void *user, const char *section, const char *name, const char *value) {
    const ConfigTable *table = (const ConfigTable *)user;

    char *end;

    for (size_t i = 0; i < table->count; i++) {
        const ConfigItem *item = &table->items[i];
        void *out = (char *)table->values + item->offset;

        if (item->name == NULL || _stricmp(section, item->section) != 0 || _stricmp(name, item->name) != 0)
            continue;

        switch (item->type) {
            case CONFIG_ITEM_STR:
                *(char **)out = _strdup(value);
                break;
            case CONFIG_ITEM_BOOL:
                *(bool *)out = strcmp(value, "true") == 0;
                break;
            case CONFIG_ITEM_INT:
                *(int *)out = strtol(value, &end, 10);
                if (end == value || *end != '\0' || errno == ERANGE) *(int *)out = item->defaultInt;
                break;
            case CONFIG_ITEM_HEX:
                *(int *)out = strtol(value, &end, 16);
                if (end == value || *end != '\0' || errno == ERANGE) *(int *)out = item->defaultInt;
                break;
            case CONFIG_ITEM_IPV4: {
                unsigned char ip_a, ip_b, ip_c, ip_d;
                if (sscanf(value, "%hhu.%hhu.%hhu.%hhu", &ip_a, &ip_b, &ip_c, &ip_d) == 4)
                    *(unsigned int *)out = (unsigned int)((ip_d << 24) | (ip_c << 16) | (ip_b << 8) | ip_a);
                else
                    *(unsigned int *)out = item->defaultIpv4;
                break;
            }
            case CONFIG_ITEM_RENAMED_STR:
                *(char **)out = _strdup(value);
                break;
            case CONFIG_ITEM_RENAMED_INT: {
                long renamed = strtol(value, &end, 10);
                if (end != value && *end == '\0') *(int *)out = renamed;
                break;
            }
            default:
                break;
        }
        return 1;
    }

    return 1;
}

void config_read_table(const ConfigTable *table) {
    ini_parse(CONFIG_PATH, handler, (void *)table);
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
    if (ini_parse(CONFIG_PATH, handler, (void *)&g_table) < 0)
    {
        MessageBox(
            NULL,
            "Kittentools is free software and is distributed free of charge.\n\nIf you paid for it, you've been scammed. Please report the seller.",
            "Warning",
            MB_ICONWARNING | MB_OK
        );
        log_error(plfBoot, "Configuration file not found at \"%s\", loading default settings", CONFIG_PATH);
    }

    /* The file is written once the game is known: KittenInit(). */
    g_fileConfig = Config;
    g_fileConfigRead = true;

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