#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mstcpip.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "network.h"
#include "symbol.h"
#include "../log/log.h"
#include "../hooks/network.h"
#include "../hooks/file.h"
#include "../devices/device.h"

unsigned long hook_inet_addr(const char *cp);
int hook_inet_aton(const char *cp, struct in_addr *inp);
int hook_inet_pton(int af, const char *src, void *dst);
char *hook_inet_ntoa(struct in_addr in);
uint16_t hook_ntohs(uint16_t netshort);
uint16_t hook_htons(uint16_t hostshort);
uint32_t hook_ntohl(uint32_t netlong);
uint32_t hook_htonl(uint32_t hostlong);
int hook_inet_aton(const char *cp, struct in_addr *inp);
int hook_socket(int af, int type, int protocol);
int hook_connect(int fd, const struct sockaddr *name, int namelen);
int hook_bind(int fd, const struct sockaddr *name, int namelen);
int hook_listen(int fd, int backlog);
int hook_accept(int fd, struct sockaddr *addr, int *addrlen);
int hook_recv(int fd, char *buf, int len, int flags);
int hook_send(int fd, const char *buf, int len, int flags);
int hook_recvfrom(int fd, char *buf, int len, int flags, struct sockaddr *from, int *fromlen);
int hook_sendto(int fd, const char *buf, int len, int flags, const struct sockaddr *to, int tolen);
int hook_setsockopt(int fd, int level, int optname, const char *optval, int optlen);
int hook_getsockopt(int fd, int level, int optname, char *optval, int *optlen);
int hook_net_close(int fd);
int hook_select(int nfds, linux_fd_set *readfds, linux_fd_set *writefds, linux_fd_set *exceptfds, struct timeval *timeout);
int hook_shutdown(int fd, int how);
int hook_Gethostbyname_r(const char *name, struct linux_hostent *ret, char *buf, size_t buflen, struct linux_hostent **result, int *h_errnop);
int hook_Gethostbyaddr_r(const void *addr, int len, int type, void *ret, char *buf, size_t buflen, void **result,
                          int *h_errnop);
struct linux_hostent *hook_Gethostbyname(const char *name);
struct linux_hostent *hook_Gethostbyaddr(const void *addr, int len, int type);
#define LINUX_FD_ZERO(set) do { \
    unsigned int __i; \
    for (__i = 0; __i < (MAX_FD_SIZE / LINUX_NFDBITS); __i++) { \
        (set)->fds_bits[__i] = 0; \
    } \
} while (0)
#define LINUX_FD_SET(fd, set) do { \
    if ((fd) >= 0 && (fd) < MAX_FD_SIZE) { \
        (set)->fds_bits[(fd) / LINUX_NFDBITS] |= (1UL << ((fd) % LINUX_NFDBITS)); \
    } \
} while (0)
#define LINUX_FD_CLR(fd, set) do { \
    if ((fd) >= 0 && (fd) < MAX_FD_SIZE) { \
        (set)->fds_bits[(fd) / LINUX_NFDBITS] &= ~(1UL << ((fd) % LINUX_NFDBITS)); \
    } \
} while (0)
#define LINUX_FD_ISSET(fd, set) ( \
    ((fd) >= 0 && (fd) < MAX_FD_SIZE) ? \
    ((set)->fds_bits[(fd) / LINUX_NFDBITS] & (1UL << ((fd) % LINUX_NFDBITS))) != 0 : \
    0 \
)

struct fd_map_entry g_fd_map[MAX_FD_SIZE];
int g_next_fake_fd = 20;

static CRITICAL_SECTION g_fd_cs;
extern struct dns_hook_entry *dns_hook_entries;
extern int dns_hook_nentries;


// all setsockopt id games used
#define LINUX_SOL_SOCKET        0x0001
#define LINUX_IPPROTO_IP        0x0000

#define LINUX_IP_TTL            0x0002
#define LINUX_SO_REUSEADDR      0x0002
#define LINUX_SO_BROADCAST      0x0006
#define LINUX_SO_SNDBUFFORCE    0x0020
#define LINUX_SO_TIMESTAMP      0x001D


// Some POSIX error codes might not be defined in MinGW's <errno.h>
#ifndef ENOTSOCK
#define ENOTSOCK EINVAL
#endif
#ifndef EDESTADDRREQ
#define EDESTADDRREQ EINVAL
#endif
#ifndef EMSGSIZE
#define EMSGSIZE EINVAL
#endif
#ifndef EPROTOTYPE
#define EPROTOTYPE EINVAL
#endif
#ifndef ENOPROTOOPT
#define ENOPROTOOPT EINVAL
#endif
#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT EINVAL
#endif
#ifndef ESOCKTNOSUPPORT
#define ESOCKTNOSUPPORT EINVAL
#endif
#ifndef EOPNOTSUPP
#define EOPNOTSUPP EINVAL
#endif
#ifndef EPFNOSUPPORT
#define EPFNOSUPPORT EINVAL
#endif
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT EINVAL
#endif
#ifndef EADDRINUSE
#define EADDRINUSE EINVAL
#endif
#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL EINVAL
#endif
#ifndef ENETDOWN
#define ENETDOWN EINVAL
#endif
#ifndef ENETUNREACH
#define ENETUNREACH EINVAL
#endif
#ifndef ENETRESET
#define ENETRESET EINVAL
#endif
#ifndef ECONNABORTED
#define ECONNABORTED EINVAL
#endif
#ifndef ECONNRESET
#define ECONNRESET EINVAL
#endif
#ifndef ENOBUFS
#define ENOBUFS EINVAL
#endif
#ifndef EISCONN
#define EISCONN EINVAL
#endif
#ifndef ENOTCONN
#define ENOTCONN EINVAL
#endif
#ifndef ESHUTDOWN
#define ESHUTDOWN EINVAL
#endif
#ifndef ETOOMANYREFS
#define ETOOMANYREFS EINVAL
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT EINVAL
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED EINVAL
#endif
#ifndef EHOSTDOWN
#define EHOSTDOWN EINVAL
#endif
#ifndef EHOSTUNREACH
#define EHOSTUNREACH EINVAL
#endif
#ifndef EPROCLIM
#define EPROCLIM EINVAL
#endif
#ifndef EUSERS
#define EUSERS EINVAL
#endif
#ifndef EDQUOT
#define EDQUOT EINVAL
#endif
#ifndef ESTALE
#define ESTALE EINVAL
#endif
#ifndef EREMOTE
#define EREMOTE EINVAL
#endif

static int mapWSAErrorToErrno(int wsaError)
{
    switch (wsaError)
    {
        case WSAEINTR:
            return EINTR;
        case WSAEBADF:
            return EBADF;
        case WSAEACCES:
            return EACCES;
        case WSAEFAULT:
            return EFAULT;
        case WSAEINVAL:
            return EINVAL;
        case WSAEMFILE:
            return EMFILE;
        case WSAEWOULDBLOCK:
            return EAGAIN;
        case WSAEINPROGRESS:
            return EINPROGRESS;
        case WSAEALREADY:
            return EALREADY;
        case WSAENOTSOCK:
            return ENOTSOCK;
        case WSAEDESTADDRREQ:
            return EDESTADDRREQ;
        case WSAEMSGSIZE:
            return EMSGSIZE;
        case WSAEPROTOTYPE:
            return EPROTOTYPE;
        case WSAENOPROTOOPT:
            return ENOPROTOOPT;
        case WSAEPROTONOSUPPORT:
            return EPROTONOSUPPORT;
        case WSAESOCKTNOSUPPORT:
            return ESOCKTNOSUPPORT;
        case WSAEOPNOTSUPP:
            return EOPNOTSUPP;
        case WSAEPFNOSUPPORT:
            return EPFNOSUPPORT;
        case WSAEAFNOSUPPORT:
            return EAFNOSUPPORT;
        case WSAEADDRINUSE:
            return EADDRINUSE;
        case WSAEADDRNOTAVAIL:
            return EADDRNOTAVAIL;
        case WSAENETDOWN:
            return ENETDOWN;
        case WSAENETUNREACH:
            return ENETUNREACH;
        case WSAENETRESET:
            return ENETRESET;
        case WSAECONNABORTED:
            return ECONNABORTED;
        case WSAECONNRESET:
            return ECONNRESET;
        case WSAENOBUFS:
            return ENOBUFS;
        case WSAEISCONN:
            return EISCONN;
        case WSAENOTCONN:
            return ENOTCONN;
        case WSAESHUTDOWN:
            return ESHUTDOWN;
        case WSAETOOMANYREFS:
            return ETOOMANYREFS;
        case WSAETIMEDOUT:
            return ETIMEDOUT;
        case WSAECONNREFUSED:
            return ECONNREFUSED;
        case WSAELOOP:
            return ELOOP;
        case WSAENAMETOOLONG:
            return ENAMETOOLONG;
        case WSAEHOSTDOWN:
            return EHOSTDOWN;
        case WSAEHOSTUNREACH:
            return EHOSTUNREACH;
        case WSAENOTEMPTY:
            return ENOTEMPTY;
        case WSAEPROCLIM:
            return EPROCLIM;
        case WSAEUSERS:
            return EUSERS;
        case WSAEDQUOT:
            return EDQUOT;
        case WSAESTALE:
            return ESTALE;
        case WSAEREMOTE:
            return EREMOTE;
        default:
            return EINVAL;
    }
}

int allocate_fd(SOCKET real_socket, int is_socket) {
    EnterCriticalSection(&g_fd_cs);
    DeviceDescriptor *device = NULL;

    for (int fd = g_next_fake_fd; fd < MAX_FD_SIZE; fd++) {
        device = getDevice(fd);
        if (device) continue;

        if (!g_fd_map[fd].used) {
            g_fd_map[fd].used = 1;
            g_fd_map[fd].fake_fd = fd;
            g_fd_map[fd].real_socket = real_socket;
            g_fd_map[fd].is_socket = is_socket;
            LeaveCriticalSection(&g_fd_cs);
            return fd;
        }
    }
    LeaveCriticalSection(&g_fd_cs);
    return -1;
}

void release_fd(int fd) {
    EnterCriticalSection(&g_fd_cs);
    if (g_fd_map[fd].used) {
        if (g_fd_map[fd].is_socket) {
            closesocket(g_fd_map[fd].real_socket);
        }
        g_fd_map[fd].used = 0;
        g_fd_map[fd].real_socket = 0;
        g_fd_map[fd].fake_fd = 0;
        g_fd_map[fd].is_socket = 0;
        g_fd_map[fd].optname_count = 0;
    }

    LeaveCriticalSection(&g_fd_cs);
}


static SOCKET fd_to_socket(int fd)
{
    if (fd < 0 || fd >= MAX_FD_SIZE || !g_fd_map[fd].used || !g_fd_map[fd].is_socket) {
        return INVALID_SOCKET;
    }
    return (SOCKET)g_fd_map[fd].real_socket;
}

static void convert_linux_to_win_fd_set(const linux_fd_set *linux_set, fd_set *win_set)
{
    if (!linux_set || !win_set) return;

    FD_ZERO(win_set);

    for (int fd = 0; fd < MAX_FD_SIZE; fd++) {
        if (LINUX_FD_ISSET(fd, linux_set)) {
            SOCKET s = fd_to_socket(fd);
            if (s != INVALID_SOCKET) {
                FD_SET(s, win_set);
            }
        }
    }
}

static void convert_win_to_linux_fd_set(const fd_set *win_set, linux_fd_set *linux_set)
{
    if (!win_set || !linux_set) return;

    LINUX_FD_ZERO(linux_set);

    for (int fd = 0; fd < MAX_FD_SIZE; fd++) {
        SOCKET s = fd_to_socket(fd);
        if (s != INVALID_SOCKET && FD_ISSET(s, win_set)) {
            LINUX_FD_SET(fd, linux_set);
        }
    }
}

void networkBridge_initBridges(void)
{
    log_debug(plfNetwork, "Initializing Network Bridges...");
    InitializeCriticalSection(&g_fd_cs);
    for (int i = 0; i < MAX_FD_SIZE; i++) {
        g_fd_map[i].used = 0;
        g_fd_map[i].fake_fd = 0;
        g_fd_map[i].real_socket = 0;
        g_fd_map[i].is_socket = 0;
        g_fd_map[i].file_handle = INVALID_HANDLE_VALUE;
        g_fd_map[i].optname_count = 0;
    }

    symbol_register("socket", hook_socket, NULL);
    symbol_register("connect", hook_connect, NULL);
    symbol_register("bind", hook_bind, NULL);
    symbol_register("listen", hook_listen, NULL);
    symbol_register("accept", hook_accept, NULL);
    symbol_register("send", hook_send, NULL);
    symbol_register("recv", hook_recv, NULL);
    symbol_register("sendto", hook_sendto, NULL);
    symbol_register("recvfrom", hook_recvfrom, NULL);
    symbol_register("shutdown", hook_shutdown, NULL);
    symbol_register("close", hook_net_close, NULL);
    symbol_register("setsockopt", hook_setsockopt, NULL);
    symbol_register("getsockopt", hook_getsockopt, NULL);
    symbol_register("inet_pton", hook_inet_pton, NULL);
    symbol_register("inet_aton", hook_inet_aton, NULL);
    symbol_register("inet_addr", hook_inet_addr, NULL);
    symbol_register("inet_ntoa", hook_inet_ntoa, NULL);
    symbol_register("ntohs", hook_ntohs, NULL);
    symbol_register("htons", hook_htons, NULL);
    symbol_register("htonl", hook_htonl, NULL);
    symbol_register("ntohl", hook_ntohl, NULL);
    symbol_register("gethostbyname_r", hook_Gethostbyname_r, NULL);
    symbol_register("gethostbyaddr_r", hook_Gethostbyaddr_r, NULL);
    symbol_register("gethostbyname", hook_Gethostbyname, NULL);
    symbol_register("gethostbyaddr", hook_Gethostbyaddr, NULL);
    symbol_register("select", hook_select, NULL);
}

unsigned long hook_inet_addr(const char *cp)
{
    return inet_addr(cp);
}

int hook_inet_aton(const char *cp, struct in_addr *inp)
{
    unsigned long addr = hook_inet_addr(cp);
    if (addr == INADDR_NONE && strcmp(cp, "255.255.255.255") != 0)
        return 0;
    if (inp)
        inp->s_addr = addr;
    return 1;
}

int hook_inet_pton(int af, const char *src, void *dst)
{
    return InetPtonA(af, src, dst);
}

char *hook_inet_ntoa(struct in_addr in)
{
    return inet_ntoa(in);
}
uint16_t hook_ntohs(uint16_t netshort)
{
    return ntohs(netshort);
}
uint16_t hook_htons(uint16_t hostshort)
{
    return htons(hostshort);
}
uint32_t hook_ntohl(uint32_t netlong)
{
    return ntohl(netlong);
}
uint32_t hook_htonl(uint32_t hostlong)
{
    return htonl(hostlong);
}

int hook_socket(int af, int type, int protocol)
{
    SOCKET s = INVALID_SOCKET;
    if (g_netFunc.socket)
        s = g_netFunc.socket(af, type, protocol);
    else
        s = socket(af, type, protocol);

    if (s == INVALID_SOCKET)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        return -1;
    }
    int fd = allocate_fd(s, 1);
    return fd;
}

int hook_connect(int fd, const struct sockaddr *name, int namelen)
{
    SOCKET s = fd_to_socket(fd);
    
    int ret = 0;
    if(g_netFunc.connect)
        ret = g_netFunc.connect(s, name, namelen);

    else
        ret = connect(s, name, namelen);

    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        return -1;
    }
    return 0;
}

int hook_bind(int fd, const struct sockaddr *name, int namelen)
{
    SOCKET s = fd_to_socket(fd);
    int ret = 0;
    if(g_netFunc.bind)
        ret = g_netFunc.bind(s, name, namelen);
    else
        ret = bind(s, name, namelen);

    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        return -1;
    }

    return 0;
}

int hook_listen(int fd, int backlog)
{
    SOCKET s = fd_to_socket(fd);
    int ret = 0;
    if(g_netFunc.listen)
        ret = g_netFunc.listen(s, backlog);
    else
        ret = listen(s, backlog);

    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        return -1;
    }

    return (int)ret;
}

int hook_accept(int fd, struct sockaddr *addr, int *addrlen)
{
    SOCKET s = fd_to_socket(fd);
    int ret = 0;
    if(g_netFunc.accept)
        ret = g_netFunc.accept(s, addr, addrlen);
    else
        ret = accept(s, addr, addrlen);

    if (ret == SOCKET_ERROR || ret == (int)INVALID_SOCKET)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        return -1;
    }

    return (int)ret;
}

int hook_recv(int fd, char *buf, int len, int flags)
{
    SOCKET s = fd_to_socket(fd);
    if (flags & 0x40) {
        flags &= ~0x40;
    }

    int ret = 0;
    if(g_netFunc.recv)
        ret = g_netFunc.recv(s, buf, len, flags);
    else
        ret = recv(s, buf, len, flags);

    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    return ret;
}

int hook_send(int fd, const char *buf, int len, int flags)
{
    SOCKET s = fd_to_socket(fd);

    flags &= ~(0x4000 | 0x40);

    int ret = 0;
    if(g_netFunc.send)
        ret = g_netFunc.send(s, buf, len, flags);
    else
        ret = send(s, buf, len, flags);

    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        return -1;
    }
    return (int)ret;
}

int hook_recvfrom(int fd, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{
    SOCKET s = fd_to_socket(fd);
    if (flags & 0x40) flags &= ~0x40;
    int ret = 0;
    if(g_netFunc.recvfrom)
        ret = g_netFunc.recvfrom(s, buf, len, flags, from, fromlen);
    else
        ret = recvfrom(s, buf, len, flags, from, fromlen);

    if (ret == SOCKET_ERROR)
    {
        log_error(plfNetwork, ">>> recvfrom failed: fd=%d, socket=%ld, len=%d, flags=%d, from=%p, fromlen=%p", fd, s, len, flags, from,
                  fromlen);
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    return ret;
}

int hook_sendto(int fd, const char *buf, int len, int flags, const struct sockaddr *to, int tolen)
{
    SOCKET s = fd_to_socket(fd);
    flags &= ~(0x4000 | 0x40);
    int dontfrag = 1;
    setsockopt(s, IPPROTO_IP, IP_DONTFRAGMENT, (const char*)&dontfrag, sizeof(dontfrag));
    int ttl = 64;
    setsockopt(s, IPPROTO_IP, IP_TTL, (const char*)&ttl, sizeof(ttl));
    int ret = 0;
    if(g_netFunc.sendto)
        ret = g_netFunc.sendto(s, buf, len, flags, to, tolen);
    else
        ret = sendto(s, buf, len, flags, to, tolen);

    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    return ret;
}

int hook_setsockopt(int fd, int level, int optname, const char *optval, int optlen)
{
    uint32_t new_optname = 0;
    uint32_t new_level = 0;
    SOCKET s = fd_to_socket(fd);

    if (level == LINUX_SOL_SOCKET)
    {
        new_level = SOL_SOCKET;
        switch(optname) {
            case 2:
                new_optname = 0x0004; break;
            case LINUX_SO_BROADCAST:
                new_optname = SO_BROADCAST; break;
            case LINUX_SO_TIMESTAMP:
            case LINUX_SO_SNDBUFFORCE:
                return 0;
            default: new_optname = optname; break;
        }
    }

    if (level == LINUX_IPPROTO_IP)
    {
        new_level = IPPROTO_IP;
        switch(optname) {
            case LINUX_IP_TTL:
                new_optname = IP_TTL; break;
            default: new_optname = optname; break;
        }
    }

    int ret = 0;
    if(g_netFunc.setsockopt)
        ret = g_netFunc.setsockopt(s, new_level, new_optname, optval, optlen);
    else
        ret = setsockopt(s, new_level, new_optname, optval, optlen);

    if (ret == SOCKET_ERROR)
    {
        if (g_fd_map[fd].optname_count < 16) {
            g_fd_map[fd].optname[g_fd_map[fd].optname_count++] = optname;
        }
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    return ret;
}

int hook_getsockopt(int fd, int level, int optname, char *optval, int *optlen)
{
    SOCKET s = fd_to_socket(fd);

    if (level == 1) {
        level = 0xFFFF;
        if (optname == 2)
            optname = 0x0004;
    }

    int ret = 0;
    if(g_netFunc.getsockopt)
        ret = g_netFunc.getsockopt(s, SOL_SOCKET, optname, optval, optlen);
    else
        ret = getsockopt(s, SOL_SOCKET, optname, optval, optlen);

    if (ret == SOCKET_ERROR)
    {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    return ret;
}

int hook_net_close(int fd)
{
    SOCKET s = fd_to_socket(fd);
    closesocket(s);
    release_fd(fd);
    return 0;
}

int hook_select(int nfds, linux_fd_set *readfds, linux_fd_set *writefds, linux_fd_set *exceptfds, struct timeval *timeout)
{
    if (nfds < 0) {
        errno = EINVAL;
        return -1;
    }

    DeviceDescriptor *device = getDevice(nfds-1);
    if (device) return device->fd;

    fd_set win_readfds, win_writefds, win_exceptfds;
    fd_set *p_readfds = NULL, *p_writefds = NULL, *p_exceptfds = NULL;

    if (readfds) {
        convert_linux_to_win_fd_set(readfds, &win_readfds);
        p_readfds = &win_readfds;
    }

    if (writefds) {
        convert_linux_to_win_fd_set(writefds, &win_writefds);
        p_writefds = &win_writefds;
    }

    if (exceptfds) {
        convert_linux_to_win_fd_set(exceptfds, &win_exceptfds);
        p_exceptfds = &win_exceptfds;
    }


    int ret = 0;
    if(g_netFunc.select)
        ret = g_netFunc.select(0, p_readfds, p_writefds, p_exceptfds, timeout);
    else
        ret = select(0, p_readfds, p_writefds, p_exceptfds, timeout);

    if (ret == SOCKET_ERROR) {
        errno = mapWSAErrorToErrno(WSAGetLastError());
        return -1;
    }

    if (readfds && p_readfds) {
        convert_win_to_linux_fd_set(p_readfds, readfds);
    }

    if (writefds && p_writefds) {
        convert_win_to_linux_fd_set(p_writefds, writefds);
    }

    if (exceptfds && p_exceptfds) {
        convert_win_to_linux_fd_set(p_exceptfds, exceptfds);
    }

    return ret;
}

int hook_shutdown(int fd, int how)
{
    SOCKET s = fd_to_socket(fd);
    int ret = 0;
    if(g_netFunc.shutdown)
        ret = g_netFunc.shutdown(s, how);
    else
        ret = shutdown(s, how);

    if (ret == SOCKET_ERROR) {
        errno = mapWSAErrorToErrno(WSAGetLastError());
    }
    return ret;
}

struct linux_hostent *hook_Gethostbyname(const char *name)
{
    static struct linux_hostent res;
    ZeroMemory(&res, sizeof(res));
    const struct dns_hook_entry *pos;

    for (int i = 0; i < dns_hook_nentries; i++)
    {
        pos = &dns_hook_entries[i];
        if (match_domain(name, pos->from))
        {
            log_debug(plfNetwork, "[DNS] Redirect: %s -> %s", pos->from, pos->to);
            name = pos->to;
        }
    }

    struct hostent *ret = gethostbyname((const char *)name);

    if (ret)
    {
        res.h_name = ret->h_name;
        res.h_aliases = ret->h_aliases;
        res.h_addrtype = (int)ret->h_addrtype;
        res.h_length = (int)ret->h_length;
        res.h_addr_list = ret->h_addr_list;
    }

    return &res;
}

int hook_Gethostbyname_r(const char *name, struct linux_hostent *ret, char *buf, size_t buflen, struct linux_hostent **result, int *h_errnop)
{
    int ret_err = 0;
    int i;
    const struct dns_hook_entry *pos;

    for (i = 0; i < dns_hook_nentries; i++)
    {
        pos = &dns_hook_entries[i];
        if (match_domain(name, pos->from))
        {
            log_debug(plfNetwork, "[DNS] Redirect: %s -> %s", pos->from, pos->to);
            name = pos->to;
        }
    }

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;

    ret_err = getaddrinfo(name, NULL, &hints, &res);
    if (ret_err != 0) {
        log_error(plfNetwork, "getaddrinfo %s failed: %s", name ? name : "(null)", gai_strerror(ret_err));
        if (h_errnop) {
            switch (ret_err) {
                case EAI_NONAME:
                    *h_errnop = HOST_NOT_FOUND;
                    break;
                case EAI_AGAIN:
                    *h_errnop = TRY_AGAIN;
                    break;
                case EAI_FAIL:
                    *h_errnop = NO_RECOVERY;
                    break;
                default:
                    *h_errnop = NO_RECOVERY;
                    break;
            }
        }
        if (result) *result = NULL;
        return -1;
    }

    int addr_count = 0;
    struct addrinfo *rp;

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET) {
            addr_count++;
        }
    }

    char *buf_ptr = buf;

    memset(ret, 0, sizeof(struct linux_hostent));

    ret->h_name = buf_ptr;
    strcpy(ret->h_name, name);
    buf_ptr += strlen(ret->h_name) + 1;

    ret->h_aliases = (char **)buf_ptr;
    ret->h_aliases[0] = NULL;
    buf_ptr += sizeof(char *);

    ret->h_addrtype = AF_INET;
    ret->h_length = sizeof(struct in_addr);

    ret->h_addr_list = (char **)buf_ptr;
    buf_ptr += (addr_count + 1) * sizeof(char *);

    int idx = 0;
    struct in_addr *addr_storage = (struct in_addr *)buf_ptr;
    for (rp = res; rp != NULL && idx < addr_count; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET) {
            struct sockaddr_in *sa = (struct sockaddr_in *)rp->ai_addr;
            memcpy(&addr_storage[idx], &sa->sin_addr, sizeof(struct in_addr));
            ret->h_addr_list[idx] = (char *)&addr_storage[idx];
            idx++;
        }
    }

    *result = ret;
    *h_errnop = 0;

    freeaddrinfo(res);
    return 0;
}

int hook_Gethostbyaddr_r(const void *addr, int len, int type, void *ret, char *buf, size_t buflen, void **result,
                          int *h_errnop)
{
    return 0;
}

struct linux_hostent *hook_Gethostbyaddr(const void *addr, int len, int type)
{
    static struct linux_hostent res;
    ZeroMemory(&res, sizeof(res));
    struct hostent *ret = gethostbyaddr((const char *)addr, len, type);

    if (ret)
    {
        res.h_name = ret->h_name;
        res.h_aliases = ret->h_aliases;
        res.h_addrtype = (int)ret->h_addrtype;
        res.h_length = (int)ret->h_length;
        res.h_addr_list = ret->h_addr_list;
    }

    return &res;
}
