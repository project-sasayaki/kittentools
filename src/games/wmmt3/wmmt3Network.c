/* Linux socket layer for WMMT3 linked play.
 *
 * The game talks to the other cabinets with Boost.Asio's select reactor:
 * UDP multicast 225.0.0.1:50765 to find them, TCP 50765 to each one found.
 * Every guest descriptor made here is a real CRT descriptor, so it can never
 * collide with a file or a board device. */
#define WIN32_LEAN_AND_MEAN
#define FD_SETSIZE 1024
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mswsock.h>
#include <iphlpapi.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "wmmt3Network.h"
#include "../../config/config.h"
#include "../../elfLoader/elfLoader.h"
#include "../../log/log.h"

/* ---- Linux ABI (i386) ---------------------------------------------------- */

#define GUEST_FD_SETSIZE 1024

enum
{
    LINUX_EINTR = 4,
    LINUX_EBADF = 9,
    LINUX_EAGAIN = 11,
    LINUX_ENOMEM = 12,
    LINUX_EACCES = 13,
    LINUX_EFAULT = 14,
    LINUX_EINVAL = 22,
    LINUX_EMFILE = 24,
    LINUX_EPIPE = 32,
    LINUX_ENOTSOCK = 88,
    LINUX_EDESTADDRREQ = 89,
    LINUX_EMSGSIZE = 90,
    LINUX_EPROTOTYPE = 91,
    LINUX_ENOPROTOOPT = 92,
    LINUX_EPROTONOSUPPORT = 93,
    LINUX_ESOCKTNOSUPPORT = 94,
    LINUX_EOPNOTSUPP = 95,
    LINUX_EAFNOSUPPORT = 97,
    LINUX_EADDRINUSE = 98,
    LINUX_EADDRNOTAVAIL = 99,
    LINUX_ENETDOWN = 100,
    LINUX_ENETUNREACH = 101,
    LINUX_ENETRESET = 102,
    LINUX_ECONNABORTED = 103,
    LINUX_ECONNRESET = 104,
    LINUX_ENOBUFS = 105,
    LINUX_EISCONN = 106,
    LINUX_ENOTCONN = 107,
    LINUX_ESHUTDOWN = 108,
    LINUX_ETIMEDOUT = 110,
    LINUX_ECONNREFUSED = 111,
    LINUX_EHOSTDOWN = 112,
    LINUX_EHOSTUNREACH = 113,
    LINUX_EALREADY = 114,
    LINUX_EINPROGRESS = 115,
};

enum
{
    LINUX_SOL_SOCKET = 1,
    LINUX_IPPROTO_IP = 0,
    LINUX_IPPROTO_TCP = 6,

    LINUX_SO_REUSEADDR = 2,
    LINUX_SO_TYPE = 3,
    LINUX_SO_ERROR = 4,
    LINUX_SO_BROADCAST = 6,
    LINUX_SO_SNDBUF = 7,
    LINUX_SO_RCVBUF = 8,
    LINUX_SO_KEEPALIVE = 9,
    LINUX_SO_LINGER = 13,

    LINUX_IP_TTL = 2,
    LINUX_IP_MULTICAST_IF = 32,
    LINUX_IP_MULTICAST_TTL = 33,
    LINUX_IP_MULTICAST_LOOP = 34,
    LINUX_IP_ADD_MEMBERSHIP = 35,
    LINUX_IP_DROP_MEMBERSHIP = 36,

    LINUX_TCP_NODELAY = 1,
};

enum
{
    LINUX_MSG_OOB = 0x1,
    LINUX_MSG_PEEK = 0x2,
    LINUX_MSG_DONTROUTE = 0x4,
    LINUX_MSG_TRUNC = 0x20,
    LINUX_MSG_DONTWAIT = 0x40,
    LINUX_MSG_WAITALL = 0x100,
    LINUX_MSG_NOSIGNAL = 0x4000,
};

enum
{
    LINUX_F_GETFD = 1,
    LINUX_F_SETFD = 2,
    LINUX_F_GETFL = 3,
    LINUX_F_SETFL = 4,
    LINUX_O_RDWR = 0x2,
    LINUX_O_NONBLOCK = 0x800,

    LINUX_FIONREAD = 0x541B,
    LINUX_FIONBIO = 0x5421,
};

typedef struct
{
    uint32_t bits[GUEST_FD_SETSIZE / 32];
} GuestFdSet;

typedef struct
{
    int onoff;
    int seconds;
} GuestLinger;

typedef struct
{
    void *base;
    uint32_t length;
} GuestIovec;

typedef struct
{
    void *name;
    uint32_t nameLength;
    GuestIovec *iov;
    uint32_t iovCount;
    void *control;
    uint32_t controlLength;
    int flags;
} GuestMsghdr;

static bool fdset_has(const GuestFdSet *set, int fd)
{
    return set && (set->bits[fd / 32] >> (fd % 32)) & 1u;
}

static void fdset_add(GuestFdSet *set, int fd)
{
    set->bits[fd / 32] |= 1u << (fd % 32);
}

static int linux_errno(int wsaError)
{
    switch (wsaError)
    {
        case WSAEINTR: return LINUX_EINTR;
        case WSAEBADF: return LINUX_EBADF;
        case WSAEACCES: return LINUX_EACCES;
        case WSAEFAULT: return LINUX_EFAULT;
        case WSAEINVAL: return LINUX_EINVAL;
        case WSAEMFILE: return LINUX_EMFILE;
        case WSAEWOULDBLOCK: return LINUX_EAGAIN;
        case WSAEINPROGRESS: return LINUX_EINPROGRESS;
        case WSAEALREADY: return LINUX_EALREADY;
        case WSAENOTSOCK: return LINUX_ENOTSOCK;
        case WSAEDESTADDRREQ: return LINUX_EDESTADDRREQ;
        case WSAEMSGSIZE: return LINUX_EMSGSIZE;
        case WSAEPROTOTYPE: return LINUX_EPROTOTYPE;
        case WSAENOPROTOOPT: return LINUX_ENOPROTOOPT;
        case WSAEPROTONOSUPPORT: return LINUX_EPROTONOSUPPORT;
        case WSAESOCKTNOSUPPORT: return LINUX_ESOCKTNOSUPPORT;
        case WSAEOPNOTSUPP: return LINUX_EOPNOTSUPP;
        case WSAEAFNOSUPPORT: return LINUX_EAFNOSUPPORT;
        case WSAEADDRINUSE: return LINUX_EADDRINUSE;
        case WSAEADDRNOTAVAIL: return LINUX_EADDRNOTAVAIL;
        case WSAENETDOWN: return LINUX_ENETDOWN;
        case WSAENETUNREACH: return LINUX_ENETUNREACH;
        case WSAENETRESET: return LINUX_ENETRESET;
        case WSAECONNABORTED: return LINUX_ECONNABORTED;
        case WSAECONNRESET: return LINUX_ECONNRESET;
        case WSAENOBUFS: return LINUX_ENOBUFS;
        case WSAEISCONN: return LINUX_EISCONN;
        case WSAENOTCONN: return LINUX_ENOTCONN;
        case WSAESHUTDOWN: return LINUX_ESHUTDOWN;
        case WSAETIMEDOUT: return LINUX_ETIMEDOUT;
        case WSAECONNREFUSED: return LINUX_ECONNREFUSED;
        case WSAEHOSTDOWN: return LINUX_EHOSTDOWN;
        case WSAEHOSTUNREACH: return LINUX_EHOSTUNREACH;
        case WSA_NOT_ENOUGH_MEMORY: return LINUX_ENOMEM;
        default: return LINUX_EINVAL;
    }
}

static int fail(int linuxError)
{
    errno = linuxError;
    return -1;
}

static int fail_wsa(void)
{
    return fail(linux_errno(WSAGetLastError()));
}

/* ---- The implementations this module stands in front of ------------------- */

static struct
{
    int (*close)(int fd);
    ssize_t (*read)(int fd, void *buffer, size_t count);
    ssize_t (*write)(int fd, const void *buffer, size_t count);
    int (*ioctl)(int fd, unsigned long request, ...);
    int (*fcntl)(int fd, int command, ...);
    int (*select)(int nfds, GuestFdSet *readfds, GuestFdSet *writefds, GuestFdSet *exceptfds,
                  struct timeval *timeout);
} g_next;

/* ---- Guest descriptors ---------------------------------------------------- */

typedef struct
{
    SOCKET socket;
    int type;
    bool used;
    bool nonblocking;
    /* A non-blocking connect() is in flight. */
    bool connecting;
} GuestSocket;

static GuestSocket g_sockets[GUEST_FD_SETSIZE];
static SRWLOCK g_socketsLock = SRWLOCK_INIT;

static bool find_socket(int fd, GuestSocket *out)
{
    if (fd < 0 || fd >= GUEST_FD_SETSIZE)
        return false;
    AcquireSRWLockShared(&g_socketsLock);
    bool used = g_sockets[fd].used;
    if (used)
        *out = g_sockets[fd];
    ReleaseSRWLockShared(&g_socketsLock);
    return used;
}

static int adopt_socket(SOCKET s, int type)
{
    /* Reserves a descriptor number the CRT will not hand to anyone else. */
    int fd = _open("NUL", _O_RDWR | _O_BINARY);
    if (fd < 0 || fd >= GUEST_FD_SETSIZE)
    {
        if (fd >= 0)
            _close(fd);
        closesocket(s);
        return fail(LINUX_EMFILE);
    }

    AcquireSRWLockExclusive(&g_socketsLock);
    g_sockets[fd] = (GuestSocket){.socket = s, .type = type, .used = true};
    ReleaseSRWLockExclusive(&g_socketsLock);
    return fd;
}

static void set_connecting(int fd, SOCKET s, bool connecting)
{
    AcquireSRWLockExclusive(&g_socketsLock);
    if (g_sockets[fd].used && g_sockets[fd].socket == s)
        g_sockets[fd].connecting = connecting;
    ReleaseSRWLockExclusive(&g_socketsLock);
}

static int set_nonblocking(int fd, SOCKET s, bool enabled)
{
    u_long mode = enabled ? 1 : 0;
    if (ioctlsocket(s, FIONBIO, &mode) == SOCKET_ERROR)
        return fail_wsa();

    AcquireSRWLockExclusive(&g_socketsLock);
    if (g_sockets[fd].used && g_sockets[fd].socket == s)
        g_sockets[fd].nonblocking = enabled;
    ReleaseSRWLockExclusive(&g_socketsLock);
    return 0;
}

/* ---- The cabinet's network interface -------------------------------------- */

/* Network byte order; 0 until an adapter is chosen. */
static uint32_t g_interfaceAddress;

static bool parse_ipv4(const char *text, uint32_t *out)
{
    unsigned a, b, c, d;
    char trailing;
    if (sscanf(text, "%u.%u.%u.%u%c", &a, &b, &c, &d, &trailing) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
        return false;
    unsigned char bytes[4] = {(unsigned char)a, (unsigned char)b, (unsigned char)c, (unsigned char)d};
    memcpy(out, bytes, sizeof(bytes));
    return true;
}

/* "a.b.c.d/n"; both in network byte order. */
static bool parse_subnet(const char *text, uint32_t *network, uint32_t *mask)
{
    unsigned a, b, c, d, prefix;
    char trailing;
    if (sscanf(text, "%u.%u.%u.%u/%u%c", &a, &b, &c, &d, &prefix, &trailing) != 5 || a > 255 || b > 255 ||
        c > 255 || d > 255 || prefix > 32)
        return false;
    unsigned char bytes[4] = {(unsigned char)a, (unsigned char)b, (unsigned char)c, (unsigned char)d};
    memcpy(network, bytes, sizeof(bytes));
    *mask = prefix ? htonl(0xFFFFFFFFu << (32 - prefix)) : 0;
    *network &= *mask;
    return true;
}

#define WHOLE_ADDRESS 0xFFFFFFFFu

/* The first connected adapter with an address in this network (one address when the mask is WHOLE_ADDRESS). */
static bool query_interface(uint32_t network, uint32_t networkMask, Wmmt3Interface *out)
{
    ULONG size = 16 * 1024;
    IP_ADAPTER_ADDRESSES *adapters = NULL;
    ULONG status = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && status == ERROR_BUFFER_OVERFLOW; ++attempt)
    {
        IP_ADAPTER_ADDRESSES *grown = (IP_ADAPTER_ADDRESSES *)realloc(adapters, size);
        if (!grown)
            break;
        adapters = grown;
        status = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                      GAA_FLAG_SKIP_DNS_SERVER, NULL, adapters, &size);
    }

    bool found = false;
    for (IP_ADAPTER_ADDRESSES *a = status == NO_ERROR ? adapters : NULL; a && !found; a = a->Next)
    {
        if (a->OperStatus != IfOperStatusUp)
            continue;

        for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u; u = u->Next)
        {
            if (u->Address.lpSockaddr->sa_family != AF_INET)
                continue;
            uint32_t address = ((const struct sockaddr_in *)u->Address.lpSockaddr)->sin_addr.s_addr;
            if ((address & networkMask) != network)
                continue;

            uint32_t mask = u->OnLinkPrefixLength ? htonl(0xFFFFFFFFu << (32 - u->OnLinkPrefixLength)) : 0;
            memset(out, 0, sizeof(*out));
            memcpy(out->address, &address, 4);
            memcpy(out->mask, &mask, 4);
            if (a->PhysicalAddressLength >= sizeof(out->mac))
                memcpy(out->mac, a->PhysicalAddress, sizeof(out->mac));
            out->link = true;
            found = true;
            break;
        }
    }
    free(adapters);
    return found;
}

bool wmmt3_network_interface(Wmmt3Interface *out)
{
    return g_interfaceAddress != 0 && query_interface(g_interfaceAddress, WHOLE_ADDRESS, out);
}

/* link_ip names the address outright; without it, it is this PC's address inside link_subnet. */
static void choose_interface(void)
{
    const char *address = Config.wmmt3.link_ip ? Config.wmmt3.link_ip : "";
    const char *subnet = Config.wmmt3.link_subnet ? Config.wmmt3.link_subnet : "";
    uint32_t network = 0, mask = WHOLE_ADDRESS;
    if (address[0])
    {
        if (!parse_ipv4(address, &network) || network == 0)
        {
            log_error(plfWmmt3, "Linked play: link_ip \"%s\" is not an IPv4 address", address);
            return;
        }
    }
    else if (!parse_subnet(subnet, &network, &mask))
    {
        log_error(plfWmmt3, "Linked play: link_subnet \"%s\" is not a network such as 192.168.1.0/24", subnet);
        return;
    }

    Wmmt3Interface nic;
    if (!query_interface(network, mask, &nic))
    {
        if (address[0])
            log_error(plfWmmt3, "Linked play: no connected adapter has the address %s", address);
        else
            log_error(plfWmmt3, "Linked play: this PC has no address in link_subnet %s; set it to the network "
                                "the cabinets share", subnet);
        return;
    }

    memcpy(&g_interfaceAddress, nic.address, sizeof(g_interfaceAddress));
    log_info(plfWmmt3, "Linked play: cabinet network on %u.%u.%u.%u (mask %u.%u.%u.%u)", nic.address[0],
             nic.address[1], nic.address[2], nic.address[3], nic.mask[0], nic.mask[1], nic.mask[2], nic.mask[3]);
}

/* ---- Socket calls --------------------------------------------------------- */

static const char *endpoint_text(const struct sockaddr *address, int length, char buffer[32])
{
    const struct sockaddr_in *in = (const struct sockaddr_in *)address;
    if (!address || length < (int)sizeof(*in) || in->sin_family != AF_INET)
        return "?";
    const unsigned char *ip = (const unsigned char *)&in->sin_addr;
    snprintf(buffer, 32, "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], ntohs(in->sin_port));
    return buffer;
}

static int wmmt3_socket(int domain, int type, int protocol)
{
    if (domain != AF_INET)
        return fail(LINUX_EAFNOSUPPORT);

    SOCKET s = socket(AF_INET, type, protocol);
    if (s == INVALID_SOCKET)
        return fail_wsa();

    if (type == SOCK_DGRAM)
    {
        /* Linux does not fail a later receive because an earlier datagram was refused. */
        BOOL reportReset = FALSE;
        DWORD returned = 0;
        if (WSAIoctl(s, SIO_UDP_CONNRESET, &reportReset, sizeof(reportReset), NULL, 0, &returned, NULL, NULL) ==
            SOCKET_ERROR)
            log_warn(plfWmmt3, "Linked play: SIO_UDP_CONNRESET failed (%d)", WSAGetLastError());

        /* The cabinet has one interface; multicast leaves through it. */
        if (g_interfaceAddress &&
            setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (const char *)&g_interfaceAddress,
                       sizeof(g_interfaceAddress)) == SOCKET_ERROR)
            log_warn(plfWmmt3, "Linked play: IP_MULTICAST_IF failed (%d)", WSAGetLastError());
    }
    int fd = adopt_socket(s, type);
    log_debug(plfWmmt3, "Linked play: socket %d opened (%s)", fd, type == SOCK_DGRAM ? "UDP" : "TCP");
    return fd;
}

static int wmmt3_bind(int fd, const struct sockaddr *address, int length)
{
    GuestSocket entry;
    if (!find_socket(fd, &entry))
        return fail(LINUX_EBADF);
    char text[32];
    if (bind(entry.socket, address, length) == SOCKET_ERROR)
    {
        log_warn(plfWmmt3, "Linked play: socket %d could not bind %s (%d)", fd, endpoint_text(address, length, text),
                 WSAGetLastError());
        return fail_wsa();
    }
    log_debug(plfWmmt3, "Linked play: socket %d bound to %s", fd, endpoint_text(address, length, text));
    return 0;
}

static int wmmt3_listen(int fd, int backlog)
{
    GuestSocket entry;
    if (!find_socket(fd, &entry))
        return fail(LINUX_EBADF);
    if (listen(entry.socket, backlog) == SOCKET_ERROR)
    {
        log_warn(plfWmmt3, "Linked play: socket %d could not listen (%d)", fd, WSAGetLastError());
        return fail_wsa();
    }
    log_debug(plfWmmt3, "Linked play: socket %d listening", fd);
    return 0;
}

static int wmmt3_accept(int fd, struct sockaddr *address, int *length)
{
    GuestSocket entry;
    if (!find_socket(fd, &entry))
        return fail(LINUX_EBADF);

    struct sockaddr_in peer;
    int peerLength = sizeof(peer);
    SOCKET accepted = accept(entry.socket, (struct sockaddr *)&peer, &peerLength);
    if (accepted == INVALID_SOCKET)
        return fail_wsa();
    if (address && length)
    {
        memcpy(address, &peer, *length < peerLength ? (size_t)*length : (size_t)peerLength);
        *length = peerLength;
    }

    /* Winsock hands the listener's non-blocking mode down; Linux does not. */
    u_long blocking = 0;
    if (ioctlsocket(accepted, FIONBIO, &blocking) == SOCKET_ERROR)
    {
        int error = WSAGetLastError();
        closesocket(accepted);
        return fail(linux_errno(error));
    }
    char text[32];
    int acceptedFd = adopt_socket(accepted, entry.type);
    log_debug(plfWmmt3, "Linked play: socket %d accepted %s as socket %d", fd,
              endpoint_text((const struct sockaddr *)&peer, peerLength, text), acceptedFd);
    return acceptedFd;
}

static int wmmt3_connect(int fd, const struct sockaddr *address, int length)
{
    GuestSocket entry;
    if (!find_socket(fd, &entry))
        return fail(LINUX_EBADF);

    char text[32];
    if (connect(entry.socket, address, length) != SOCKET_ERROR)
    {
        log_debug(plfWmmt3, "Linked play: socket %d connected to %s", fd, endpoint_text(address, length, text));
        return 0;
    }

    int error = WSAGetLastError();
    if (error != WSAEWOULDBLOCK)
    {
        log_debug(plfWmmt3, "Linked play: socket %d could not connect to %s (%d)", fd,
                  endpoint_text(address, length, text), error);
        return fail(linux_errno(error));
    }
    log_debug(plfWmmt3, "Linked play: socket %d connecting to %s", fd, endpoint_text(address, length, text));
    set_connecting(fd, entry.socket, true);
    return fail(LINUX_EINPROGRESS);
}

static int wmmt3_getsockname(int fd, struct sockaddr *address, int *length)
{
    GuestSocket entry;
    if (!find_socket(fd, &entry))
        return fail(LINUX_EBADF);
    return getsockname(entry.socket, address, length) == SOCKET_ERROR ? fail_wsa() : 0;
}

static int wmmt3_getpeername(int fd, struct sockaddr *address, int *length)
{
    GuestSocket entry;
    if (!find_socket(fd, &entry))
        return fail(LINUX_EBADF);
    return getpeername(entry.socket, address, length) == SOCKET_ERROR ? fail_wsa() : 0;
}

/* ---- Socket options ------------------------------------------------------- */

typedef enum
{
    VALUE_INT,
    VALUE_LINGER,
    /* Linux also takes these as a single byte. */
    VALUE_BYTE_OR_INT,
    /* struct in_addr naming a local interface. */
    VALUE_INTERFACE,
    /* struct ip_mreq. */
    VALUE_MEMBERSHIP,
} OptionValue;

typedef struct
{
    int linuxLevel;
    int linuxName;
    int level;
    int name;
    OptionValue value;
} SocketOption;

static const SocketOption g_socketOptions[] = {
    {LINUX_SOL_SOCKET, LINUX_SO_REUSEADDR, SOL_SOCKET, SO_REUSEADDR, VALUE_INT},
    {LINUX_SOL_SOCKET, LINUX_SO_TYPE, SOL_SOCKET, SO_TYPE, VALUE_INT},
    {LINUX_SOL_SOCKET, LINUX_SO_ERROR, SOL_SOCKET, SO_ERROR, VALUE_INT},
    {LINUX_SOL_SOCKET, LINUX_SO_BROADCAST, SOL_SOCKET, SO_BROADCAST, VALUE_INT},
    {LINUX_SOL_SOCKET, LINUX_SO_SNDBUF, SOL_SOCKET, SO_SNDBUF, VALUE_INT},
    {LINUX_SOL_SOCKET, LINUX_SO_RCVBUF, SOL_SOCKET, SO_RCVBUF, VALUE_INT},
    {LINUX_SOL_SOCKET, LINUX_SO_KEEPALIVE, SOL_SOCKET, SO_KEEPALIVE, VALUE_INT},
    {LINUX_SOL_SOCKET, LINUX_SO_LINGER, SOL_SOCKET, SO_LINGER, VALUE_LINGER},
    {LINUX_IPPROTO_IP, LINUX_IP_TTL, IPPROTO_IP, IP_TTL, VALUE_INT},
    {LINUX_IPPROTO_IP, LINUX_IP_MULTICAST_IF, IPPROTO_IP, IP_MULTICAST_IF, VALUE_INTERFACE},
    {LINUX_IPPROTO_IP, LINUX_IP_MULTICAST_TTL, IPPROTO_IP, IP_MULTICAST_TTL, VALUE_BYTE_OR_INT},
    {LINUX_IPPROTO_IP, LINUX_IP_MULTICAST_LOOP, IPPROTO_IP, IP_MULTICAST_LOOP, VALUE_BYTE_OR_INT},
    {LINUX_IPPROTO_IP, LINUX_IP_ADD_MEMBERSHIP, IPPROTO_IP, IP_ADD_MEMBERSHIP, VALUE_MEMBERSHIP},
    {LINUX_IPPROTO_IP, LINUX_IP_DROP_MEMBERSHIP, IPPROTO_IP, IP_DROP_MEMBERSHIP, VALUE_MEMBERSHIP},
    {LINUX_IPPROTO_TCP, LINUX_TCP_NODELAY, IPPROTO_TCP, TCP_NODELAY, VALUE_INT},
};

static const SocketOption *find_option(int level, int name)
{
    for (size_t i = 0; i < sizeof(g_socketOptions) / sizeof(g_socketOptions[0]); ++i)
    {
        if (g_socketOptions[i].linuxLevel == level && g_socketOptions[i].linuxName == name)
            return &g_socketOptions[i];
    }
    log_warn(plfWmmt3, "Linked play: socket option %d/%d is not supported", level, name);
    return NULL;
}

static int wmmt3_setsockopt(int fd, int level, int name, const void *value, unsigned length)
{
    GuestSocket entry;
    if (!find_socket(fd, &entry))
        return fail(LINUX_EBADF);
    const SocketOption *option = find_option(level, name);
    if (!option)
        return fail(LINUX_ENOPROTOOPT);
    if (!value)
        return fail(LINUX_EFAULT);

    /* A Linux listener with SO_REUSEADDR only skips TIME_WAIT, which Winsock never enforces;
     * Winsock's SO_REUSEADDR would let a second process take the port. */
    if (option->name == SO_REUSEADDR && option->level == SOL_SOCKET && entry.type == SOCK_STREAM)
        return length >= sizeof(int) ? 0 : fail(LINUX_EINVAL);

    union
    {
        int number;
        LINGER linger;
        struct in_addr nic;
        struct ip_mreq membership;
    } host;
    int hostLength = 0;

    switch (option->value)
    {
        case VALUE_INT:
            if (length < sizeof(int))
                return fail(LINUX_EINVAL);
            memcpy(&host.number, value, sizeof(int));
            hostLength = sizeof(int);
            break;
        case VALUE_BYTE_OR_INT:
            if (length >= sizeof(int))
                memcpy(&host.number, value, sizeof(int));
            else if (length == 1)
                host.number = *(const unsigned char *)value;
            else
                return fail(LINUX_EINVAL);
            hostLength = sizeof(int);
            break;
        case VALUE_LINGER:
        {
            GuestLinger guest;
            if (length < sizeof(guest))
                return fail(LINUX_EINVAL);
            memcpy(&guest, value, sizeof(guest));
            host.linger.l_onoff = guest.onoff ? 1 : 0;
            host.linger.l_linger = guest.seconds < 0 ? 0 : guest.seconds > 0xFFFF ? 0xFFFF : (u_short)guest.seconds;
            hostLength = sizeof(host.linger);
            break;
        }
        case VALUE_INTERFACE:
            if (length < sizeof(host.nic))
                return fail(LINUX_EINVAL);
            memcpy(&host.nic, value, sizeof(host.nic));
            if (host.nic.s_addr == INADDR_ANY)
                host.nic.s_addr = g_interfaceAddress;
            hostLength = sizeof(host.nic);
            break;
        case VALUE_MEMBERSHIP:
            if (length < sizeof(host.membership))
                return fail(LINUX_EINVAL);
            memcpy(&host.membership, value, sizeof(host.membership));
            /* "Any interface" is the cabinet's one interface. */
            if (host.membership.imr_interface.s_addr == INADDR_ANY)
                host.membership.imr_interface.s_addr = g_interfaceAddress;
            hostLength = sizeof(host.membership);
            break;
    }

    if (setsockopt(entry.socket, option->level, option->name, (const char *)&host, hostLength) == SOCKET_ERROR)
        return fail_wsa();
    return 0;
}

static int wmmt3_getsockopt(int fd, int level, int name, void *value, unsigned *length)
{
    GuestSocket entry;
    if (!find_socket(fd, &entry))
        return fail(LINUX_EBADF);
    const SocketOption *option = find_option(level, name);
    if (!option)
        return fail(LINUX_ENOPROTOOPT);
    if (!value || !length)
        return fail(LINUX_EFAULT);

    if (option->value == VALUE_LINGER)
    {
        LINGER host;
        int hostLength = sizeof(host);
        if (*length < sizeof(GuestLinger))
            return fail(LINUX_EINVAL);
        if (getsockopt(entry.socket, option->level, option->name, (char *)&host, &hostLength) == SOCKET_ERROR)
            return fail_wsa();
        GuestLinger guest = {.onoff = host.l_onoff, .seconds = host.l_linger};
        memcpy(value, &guest, sizeof(guest));
        *length = sizeof(guest);
        return 0;
    }

    if (option->value == VALUE_INT || option->value == VALUE_BYTE_OR_INT)
    {
        int host = 0;
        int hostLength = sizeof(host);
        if (*length < sizeof(int))
            return fail(LINUX_EINVAL);
        if (getsockopt(entry.socket, option->level, option->name, (char *)&host, &hostLength) == SOCKET_ERROR)
            return fail_wsa();
        if (option->name == SO_ERROR && option->level == SOL_SOCKET && host != 0)
            host = linux_errno(host);
        memcpy(value, &host, sizeof(host));
        *length = sizeof(host);
        return 0;
    }

    int hostLength = (int)*length;
    if (getsockopt(entry.socket, option->level, option->name, (char *)value, &hostLength) == SOCKET_ERROR)
        return fail_wsa();
    *length = (unsigned)hostLength;
    return 0;
}

/* ---- Data transfer -------------------------------------------------------- */

static bool host_flags(int flags, const GuestSocket *entry, int *out)
{
    const int known = LINUX_MSG_OOB | LINUX_MSG_PEEK | LINUX_MSG_DONTROUTE | LINUX_MSG_DONTWAIT |
                      LINUX_MSG_WAITALL | LINUX_MSG_NOSIGNAL;
    if (flags & ~known)
    {
        log_error(plfWmmt3, "Linked play: message flags 0x%x are not supported", flags & ~known);
        errno = LINUX_EINVAL;
        return false;
    }
    if ((flags & LINUX_MSG_DONTWAIT) && !entry->nonblocking)
    {
        log_error(plfWmmt3, "Linked play: MSG_DONTWAIT on a blocking socket is not supported");
        errno = LINUX_EOPNOTSUPP;
        return false;
    }

    /* MSG_NOSIGNAL has nothing to suppress: Winsock raises no SIGPIPE. */
    *out = (flags & LINUX_MSG_OOB ? MSG_OOB : 0) | (flags & LINUX_MSG_PEEK ? MSG_PEEK : 0) |
           (flags & LINUX_MSG_DONTROUTE ? MSG_DONTROUTE : 0) | (flags & LINUX_MSG_WAITALL ? MSG_WAITALL : 0);
    return true;
}

static ssize_t wmmt3_sendto(int fd, const void *buffer, size_t length, int flags, const struct sockaddr *to,
                            int toLength)
{
    GuestSocket entry;
    int hostFlags;
    if (!find_socket(fd, &entry))
        return fail(LINUX_EBADF);
    if (!host_flags(flags, &entry, &hostFlags))
        return -1;

    int sent = sendto(entry.socket, (const char *)buffer, (int)length, hostFlags, to, to ? toLength : 0);
    return sent == SOCKET_ERROR ? fail_wsa() : sent;
}

static ssize_t wmmt3_send(int fd, const void *buffer, size_t length, int flags)
{
    return wmmt3_sendto(fd, buffer, length, flags, NULL, 0);
}

static ssize_t wmmt3_recvfrom(int fd, void *buffer, size_t length, int flags, struct sockaddr *from, int *fromLength)
{
    GuestSocket entry;
    int hostFlags;
    if (!find_socket(fd, &entry))
        return fail(LINUX_EBADF);
    if (!host_flags(flags, &entry, &hostFlags))
        return -1;

    int got = recvfrom(entry.socket, (char *)buffer, (int)length, hostFlags, from, from ? fromLength : NULL);
    if (got != SOCKET_ERROR)
        return got;
    /* Linux cuts an oversized datagram to the buffer without an error. */
    if (WSAGetLastError() == WSAEMSGSIZE)
        return (ssize_t)length;
    return fail_wsa();
}

static ssize_t wmmt3_recv(int fd, void *buffer, size_t length, int flags)
{
    return wmmt3_recvfrom(fd, buffer, length, flags, NULL, NULL);
}

#define STACK_BUFFERS 16
#define LINUX_IOV_MAX 1024

static WSABUF *host_buffers(const GuestMsghdr *message, WSABUF stack[STACK_BUFFERS], size_t *total)
{
    WSABUF *buffers = stack;
    if (message->iovCount > STACK_BUFFERS)
    {
        buffers = (WSABUF *)malloc(message->iovCount * sizeof(WSABUF));
        if (!buffers)
            return NULL;
    }
    *total = 0;
    for (uint32_t i = 0; i < message->iovCount; ++i)
    {
        buffers[i].buf = (char *)message->iov[i].base;
        buffers[i].len = message->iov[i].length;
        *total += message->iov[i].length;
    }
    return buffers;
}

static ssize_t wmmt3_sendmsg(int fd, const GuestMsghdr *message, int flags)
{
    GuestSocket entry;
    int hostFlags;
    if (!find_socket(fd, &entry))
        return fail(LINUX_EBADF);
    if (!message)
        return fail(LINUX_EFAULT);
    if (message->iovCount > LINUX_IOV_MAX)
        return fail(LINUX_EMSGSIZE);
    if (!host_flags(flags, &entry, &hostFlags))
        return -1;

    WSABUF stack[STACK_BUFFERS];
    size_t total;
    WSABUF *buffers = host_buffers(message, stack, &total);
    if (!buffers)
        return fail(LINUX_ENOMEM);

    DWORD sent = 0;
    int status = WSASendTo(entry.socket, buffers, message->iovCount, &sent, (DWORD)hostFlags,
                           (const struct sockaddr *)message->name, message->name ? (int)message->nameLength : 0,
                           NULL, NULL);
    int error = status == SOCKET_ERROR ? WSAGetLastError() : 0;
    if (buffers != stack)
        free(buffers);
    return error ? fail(linux_errno(error)) : (ssize_t)sent;
}

static ssize_t wmmt3_recvmsg(int fd, GuestMsghdr *message, int flags)
{
    GuestSocket entry;
    int hostFlags;
    if (!find_socket(fd, &entry))
        return fail(LINUX_EBADF);
    if (!message)
        return fail(LINUX_EFAULT);
    if (message->iovCount > LINUX_IOV_MAX)
        return fail(LINUX_EMSGSIZE);
    if (!host_flags(flags, &entry, &hostFlags))
        return -1;

    WSABUF stack[STACK_BUFFERS];
    size_t total;
    WSABUF *buffers = host_buffers(message, stack, &total);
    if (!buffers)
        return fail(LINUX_ENOMEM);

    DWORD got = 0;
    DWORD ioFlags = (DWORD)hostFlags;
    int nameLength = (int)message->nameLength;
    int status = WSARecvFrom(entry.socket, buffers, message->iovCount, &got, &ioFlags,
                             (struct sockaddr *)message->name, message->name ? &nameLength : NULL, NULL, NULL);
    int error = status == SOCKET_ERROR ? WSAGetLastError() : 0;
    if (buffers != stack)
        free(buffers);

    message->flags = 0;
    if (error == WSAEMSGSIZE)
    {
        got = (DWORD)total;
        message->flags = LINUX_MSG_TRUNC;
    }
    else if (error)
        return fail(linux_errno(error));

    /* A connected stream reports no source address. */
    message->nameLength = message->name && entry.type == SOCK_DGRAM ? (uint32_t)nameLength : 0;
    message->controlLength = 0;
    return (ssize_t)got;
}

/* ---- Descriptor calls shared with files and boards ------------------------ */

static ssize_t wmmt3_read(int fd, void *buffer, size_t count)
{
    GuestSocket entry;
    if (!find_socket(fd, &entry))
        return g_next.read(fd, buffer, count);
    int got = recv(entry.socket, (char *)buffer, (int)count, 0);
    return got == SOCKET_ERROR ? fail_wsa() : got;
}

static ssize_t wmmt3_write(int fd, const void *buffer, size_t count)
{
    GuestSocket entry;
    if (!find_socket(fd, &entry))
        return g_next.write(fd, buffer, count);
    int sent = send(entry.socket, (const char *)buffer, (int)count, 0);
    return sent == SOCKET_ERROR ? fail_wsa() : sent;
}

static int wmmt3_close(int fd)
{
    SOCKET s = INVALID_SOCKET;
    if (fd >= 0 && fd < GUEST_FD_SETSIZE)
    {
        AcquireSRWLockExclusive(&g_socketsLock);
        if (g_sockets[fd].used)
        {
            s = g_sockets[fd].socket;
            g_sockets[fd] = (GuestSocket){0};
        }
        ReleaseSRWLockExclusive(&g_socketsLock);
    }
    if (s == INVALID_SOCKET)
        return g_next.close(fd);

    int status = closesocket(s);
    int error = status == SOCKET_ERROR ? WSAGetLastError() : 0;
    _close(fd);
    log_debug(plfWmmt3, "Linked play: socket %d closed", fd);
    return error ? fail(linux_errno(error)) : 0;
}

static int wmmt3_ioctl(int fd, unsigned long request, void *argument)
{
    GuestSocket entry;
    if (!find_socket(fd, &entry))
        return g_next.ioctl(fd, request, argument);

    switch (request)
    {
        case LINUX_FIONBIO:
            if (!argument)
                return fail(LINUX_EFAULT);
            return set_nonblocking(fd, entry.socket, *(const int *)argument != 0);
        case LINUX_FIONREAD:
        {
            u_long pending = 0;
            if (!argument)
                return fail(LINUX_EFAULT);
            if (ioctlsocket(entry.socket, FIONREAD, &pending) == SOCKET_ERROR)
                return fail_wsa();
            *(int *)argument = (int)pending;
            return 0;
        }
        default:
            return g_next.ioctl(fd, request, argument);
    }
}

static int wmmt3_fcntl(int fd, int command, long argument)
{
    GuestSocket entry;
    if (!find_socket(fd, &entry))
        return g_next.fcntl(fd, command, argument);

    switch (command)
    {
        case LINUX_F_GETFL:
            return LINUX_O_RDWR | (entry.nonblocking ? LINUX_O_NONBLOCK : 0);
        case LINUX_F_SETFL:
            return set_nonblocking(fd, entry.socket, (argument & LINUX_O_NONBLOCK) != 0);
        case LINUX_F_GETFD:
        case LINUX_F_SETFD:
            /* Close-on-exec: the guest never execs. */
            return 0;
        default:
            log_error(plfWmmt3, "Linked play: fcntl command %d on a socket is not supported", command);
            return fail(LINUX_EINVAL);
    }
}

/* Winsock's select() takes sockets only, so a pipe has to be a pair of them. */
static bool connected_pair(SOCKET pair[2])
{
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    struct sockaddr_in peer, local;
    int length = sizeof(address);
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    pair[0] = pair[1] = INVALID_SOCKET;

    bool ok = listener != INVALID_SOCKET &&
              bind(listener, (const struct sockaddr *)&address, sizeof(address)) != SOCKET_ERROR &&
              listen(listener, 1) != SOCKET_ERROR &&
              getsockname(listener, (struct sockaddr *)&address, &length) != SOCKET_ERROR &&
              (pair[1] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) != INVALID_SOCKET &&
              connect(pair[1], (const struct sockaddr *)&address, sizeof(address)) != SOCKET_ERROR;
    if (ok)
    {
        length = sizeof(peer);
        pair[0] = accept(listener, (struct sockaddr *)&peer, &length);
        length = sizeof(local);
        /* Only our own writer may be on the other end. */
        ok = pair[0] != INVALID_SOCKET &&
             getsockname(pair[1], (struct sockaddr *)&local, &length) != SOCKET_ERROR &&
             peer.sin_addr.s_addr == local.sin_addr.s_addr && peer.sin_port == local.sin_port;
    }

    int error = ok ? 0 : WSAGetLastError();
    if (listener != INVALID_SOCKET)
        closesocket(listener);
    if (ok)
    {
        BOOL noDelay = TRUE;
        setsockopt(pair[1], IPPROTO_TCP, TCP_NODELAY, (const char *)&noDelay, sizeof(noDelay));
        return true;
    }
    for (int i = 0; i < 2; ++i)
    {
        if (pair[i] != INVALID_SOCKET)
            closesocket(pair[i]);
    }
    WSASetLastError(error);
    return false;
}

static int wmmt3_pipe(int fds[2])
{
    SOCKET pair[2];
    if (!fds)
        return fail(LINUX_EFAULT);
    if (!connected_pair(pair))
        return fail_wsa();

    int reader = adopt_socket(pair[0], SOCK_STREAM);
    if (reader < 0)
    {
        closesocket(pair[1]);
        return -1;
    }
    int writer = adopt_socket(pair[1], SOCK_STREAM);
    if (writer < 0)
    {
        wmmt3_close(reader);
        return fail(LINUX_EMFILE);
    }
    fds[0] = reader;
    fds[1] = writer;
    return 0;
}

static int wmmt3_select(int nfds, GuestFdSet *readfds, GuestFdSet *writefds, GuestFdSet *exceptfds,
                        struct timeval *timeout)
{
    if (nfds < 0 || (timeout && (timeout->tv_sec < 0 || timeout->tv_usec < 0)))
        return fail(LINUX_EINVAL);
    int limit = nfds < GUEST_FD_SETSIZE ? nfds : GUEST_FD_SETSIZE;

    fd_set hostRead, hostWrite, hostExcept;
    FD_ZERO(&hostRead);
    FD_ZERO(&hostWrite);
    FD_ZERO(&hostExcept);
    int ours = 0, others = 0;

    for (int fd = 0; fd < limit; ++fd)
    {
        bool wantRead = fdset_has(readfds, fd), wantWrite = fdset_has(writefds, fd);
        bool wantExcept = fdset_has(exceptfds, fd);
        if (!wantRead && !wantWrite && !wantExcept)
            continue;

        GuestSocket entry;
        if (!find_socket(fd, &entry))
        {
            ++others;
            continue;
        }
        ++ours;
        if (wantRead)
            FD_SET(entry.socket, &hostRead);
        if (wantWrite)
            FD_SET(entry.socket, &hostWrite);
        /* Winsock reports a refused connect as an exception, Linux as writable. */
        if (wantExcept || (wantWrite && entry.connecting))
            FD_SET(entry.socket, &hostExcept);
    }

    if (others)
    {
        if (!ours)
            return g_next.select(nfds, readfds, writefds, exceptfds, timeout);
        log_error(plfWmmt3, "Linked play: select() over sockets and other descriptors is not supported");
        return fail(LINUX_EBADF);
    }

    if (!ours)
    {
        /* Nothing to watch: select() is a sleep. */
        Sleep(timeout ? (DWORD)(timeout->tv_sec * 1000 + (timeout->tv_usec + 999) / 1000) : INFINITE);
        return 0;
    }

    if (select(0, &hostRead, &hostWrite, &hostExcept, timeout) == SOCKET_ERROR)
        return fail_wsa();

    GuestFdSet readyRead = {0}, readyWrite = {0}, readyExcept = {0};
    int ready = 0;
    for (int fd = 0; fd < limit; ++fd)
    {
        bool wantRead = fdset_has(readfds, fd), wantWrite = fdset_has(writefds, fd);
        bool wantExcept = fdset_has(exceptfds, fd);
        GuestSocket entry;
        if ((!wantRead && !wantWrite && !wantExcept) || !find_socket(fd, &entry))
            continue;

        bool excepted = FD_ISSET(entry.socket, &hostExcept) != 0;
        bool connectFailed = entry.connecting && excepted;
        bool writable = FD_ISSET(entry.socket, &hostWrite) != 0 || connectFailed;
        bool readable = FD_ISSET(entry.socket, &hostRead) != 0 || connectFailed;

        if (entry.connecting && writable)
            set_connecting(fd, entry.socket, false);
        if (wantRead && readable)
        {
            fdset_add(&readyRead, fd);
            ++ready;
        }
        if (wantWrite && writable)
        {
            fdset_add(&readyWrite, fd);
            ++ready;
        }
        if (wantExcept && excepted && !connectFailed)
        {
            fdset_add(&readyExcept, fd);
            ++ready;
        }
    }

    if (readfds)
        *readfds = readyRead;
    if (writefds)
        *writefds = readyWrite;
    if (exceptfds)
        *exceptfds = readyExcept;
    return ready;
}

/* ---- Installation --------------------------------------------------------- */

void wmmt3_network_init(void)
{
    static const struct
    {
        const char *name;
        void *replacement;
        void **next;
    } imports[] = {
        {"socket", wmmt3_socket, NULL},
        {"bind", wmmt3_bind, NULL},
        {"listen", wmmt3_listen, NULL},
        {"accept", wmmt3_accept, NULL},
        {"connect", wmmt3_connect, NULL},
        {"getsockname", wmmt3_getsockname, NULL},
        {"getpeername", wmmt3_getpeername, NULL},
        {"setsockopt", wmmt3_setsockopt, NULL},
        {"getsockopt", wmmt3_getsockopt, NULL},
        {"send", wmmt3_send, NULL},
        {"sendto", wmmt3_sendto, NULL},
        {"sendmsg", wmmt3_sendmsg, NULL},
        {"recv", wmmt3_recv, NULL},
        {"recvfrom", wmmt3_recvfrom, NULL},
        {"recvmsg", wmmt3_recvmsg, NULL},
        {"pipe", wmmt3_pipe, NULL},
        {"read", wmmt3_read, (void **)&g_next.read},
        {"write", wmmt3_write, (void **)&g_next.write},
        {"close", wmmt3_close, (void **)&g_next.close},
        {"ioctl", wmmt3_ioctl, (void **)&g_next.ioctl},
        {"fcntl", wmmt3_fcntl, (void **)&g_next.fcntl},
        {"select", wmmt3_select, (void **)&g_next.select},
    };

    choose_interface();

    for (size_t i = 0; i < sizeof(imports) / sizeof(imports[0]); ++i)
    {
        void *previous = elf_interpose_import(imports[i].name, imports[i].replacement);
        if (imports[i].next)
            *imports[i].next = previous;
    }
    log_info(plfWmmt3, "Linked play: on (the game's own cabinet networking)");
}
