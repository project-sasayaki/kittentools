#pragma once

#include <winsock2.h>

#ifndef SIOCGIFFLAGS
#define SIOCGIFFLAGS 0x8913
#endif
#ifndef SIOCGIFADDR
#define SIOCGIFADDR 0x8915
#endif
#ifndef SIOCGIFNETMASK
#define SIOCGIFNETMASK 0x891B
#endif

#define MAX_DNS_LENGTH 256
struct dns_hook_entry
{
    char from[MAX_DNS_LENGTH];
    char to[MAX_DNS_LENGTH];
};



typedef struct
{
    int (*socket)(int af, int type, int protocol);
    int (*connect)(SOCKET s, const struct sockaddr *name, int namelen);
    int (*bind)(SOCKET s, const struct sockaddr *name, int namelen);
    int (*listen)(SOCKET s, int backlog);
    int (*accept)(SOCKET s, struct sockaddr *addr, int *addrlen);
    int (*recv)(SOCKET s, char *buf, int len, int flags);
    int (*send)(SOCKET s, const char *buf, int len, int flags);
    int (*recvfrom)(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen);
    int (*sendto)(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen);
    int (*setsockopt)(SOCKET s, int level, int optname, const char *optval, int optlen);
    int (*getsockopt)(SOCKET s, int level, int optname, char *optval, int *optlen);
    int (*close)(SOCKET s);
    int (*select)(int nfds,fd_set *readfds,fd_set *writefds,fd_set *exceptfds,const TIMEVAL *timeout);
    int (*shutdown)(SOCKET s, int how);
} netFunc;


extern netFunc g_netFunc;
void InitNetworkBridge(netFunc *netFunc);
void dns_entry_init();
bool match_domain(const char *target, const char *pattern);
int handleNetworkIoctl(int fd, unsigned long request, void *argp);