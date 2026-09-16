#pragma once

#include <stdint.h>
#include <stddef.h>
#include <winsock2.h>
#include <windows.h>

#define MAX_FD_SIZE 1024
#define LINUX_NFDBITS (8 * sizeof(unsigned long))

struct linux_hostent {
    char* h_name;
    char** h_aliases;
    int h_addrtype;
    int h_length;
    char** h_addr_list;
};

typedef struct {
    unsigned long fds_bits[MAX_FD_SIZE / LINUX_NFDBITS];
} linux_fd_set;

struct fd_map_entry {
    int used;
    int fake_fd;
    SOCKET real_socket;
    int is_socket;
    HANDLE file_handle;
    int optname[16];
    int optname_count;
};

void networkBridge_initBridges(void);