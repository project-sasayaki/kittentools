#include "../config/config.h"
#include "../config/config.h"
#include "../log/log.h"
#include "network.h"
#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>


netFunc g_netFunc = {0};

struct dns_hook_entry *dns_hook_entries;
int dns_hook_nentries;

int dns_hook_push(const char *from_src, const char *to_src)
{
    struct dns_hook_entry *newmem;
    struct dns_hook_entry *newitem;

    newmem = realloc(dns_hook_entries, (dns_hook_nentries + 1) * sizeof(struct dns_hook_entry));

    if (newmem == NULL)
    {
        return -1;
    }

    dns_hook_entries = newmem;
    newitem = &newmem[dns_hook_nentries++];
    strcpy(newitem->from, from_src);
    strcpy(newitem->to, to_src);

    return 1;
}

void InitNetworkBridge(netFunc *netFunc)
{
    g_netFunc = *netFunc;
    log_debug(plfBoot, "Game specific network functions initialized.");
}


void dns_entry_init()
{
    int ret = 0;
    ret = dns_hook_push("tenporouter.loc", Config.network.router);
    if (ret == -1)
        return;

    ret = dns_hook_push("bbrouter.loc", Config.network.router);
    if (ret == -1)
        return;

    ret = dns_hook_push("naominet.jp", Config.network.redirect);
    if (ret == -1)
        return;

    ret = dns_hook_push("sc.naominet.jp", Config.network.redirect);
    if (ret == -1)
        return;

    ret = dns_hook_push("ib.naominet.jp", Config.network.redirect);
    if (ret == -1)
        return;
}

bool match_domain(const char *target, const char *pattern)
{
    if (strcmp(pattern, target) == 0)
    {
        return true;
    }

    int pattern_ptr_index = 0;
    int target_ptr_index = 0;

    while (pattern[pattern_ptr_index] != '\0' && target[target_ptr_index] != '\0')
    {
        if (pattern[pattern_ptr_index] == '*')
        {
            pattern_ptr_index++; // Check next character for wildcard match.

            while (pattern[pattern_ptr_index] != target[target_ptr_index])
            {
                target_ptr_index++;

                if (target[target_ptr_index] == '\0')
                    return false;
            }
        }
        else if (pattern[pattern_ptr_index] != target[target_ptr_index])
        {
            return false;
        }
        else
        {
            pattern_ptr_index++;
            target_ptr_index++;
        }
    }

    return pattern[pattern_ptr_index] == '\0' && target[target_ptr_index] == '\0';
}

int handleNetworkIoctl(int fd, unsigned long request, void *argp)
{
    uint32_t host_addr = getNetHostAddr();
    uint32_t net_mask = getNetHostMask();
    struct ifreq {
        char ifr_name[16]; 
        union {
            struct sockaddr ifr_addr;      
            struct sockaddr ifr_netmask;   
            struct sockaddr ifr_broadaddr;
            short ifr_flags;             
            int ifr_ifindex;
        } ifr_ifru;
    };
    
    #define ifr_addr ifr_ifru.ifr_addr
    #define ifr_netmask ifr_ifru.ifr_netmask
    #define ifr_flags ifr_ifru.ifr_flags
    
    struct ifreq *ifr = (struct ifreq *)argp;
    
    switch (request) {
        case SIOCGIFFLAGS:
            ifr->ifr_flags = 0x0043; // IFF_UP | IFF_RUNNING | IFF_BROADCAST
            break;
            
        case SIOCGIFADDR:
            {
                if (host_addr)
                {
                    struct sockaddr_in *addr = (struct sockaddr_in *)&ifr->ifr_addr;
                    addr->sin_family = AF_INET;
                    addr->sin_addr.s_addr = host_addr;
                }
            }
            break;
            
        case SIOCGIFNETMASK:
            {
                if (net_mask)
                {
                    struct sockaddr_in *netmask = (struct sockaddr_in *)&ifr->ifr_netmask;
                    netmask->sin_family = AF_INET;
                    netmask->sin_addr.s_addr = net_mask;
                }
            }
            break;
            
        default:
            log_info(plfNetwork, "NetworkIoctl: Unsupported ioctl request 0x%lx\n", request);
            return -1;
    }
    
    return 0;
}