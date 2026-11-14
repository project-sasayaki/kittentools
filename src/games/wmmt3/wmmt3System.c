#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <direct.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wmmt3System.h"
#include "wmmt3Network.h"
#include "../../config/config.h"
#include "../../hooks/file.h"
#include "../../log/log.h"

static void host_path(const char *guestPath, char *out, size_t outSize)
{
    char mapped[KITTEN_PATH_MAX];
    const char *path = mapGuestTmpPath(guestPath, mapped);
    if (strcmp(guestPath, "/tmp") == 0)
    {
        getKittenPath(guestPath, mapped);
        path = mapped;
    }
    snprintf(out, outSize, "%s", path);
}

static void make_dirs(const char *hostPath)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s", hostPath);
    for (char *p = path + 1; *p; ++p)
    {
        if (*p == '/' || *p == '\\')
        {
            char saved = *p;
            *p = '\0';
            _mkdir(path);
            *p = saved;
        }
    }
    _mkdir(path);
}

static void make_guest_dirs(const char *guestPath)
{
    char path[MAX_PATH];
    host_path(guestPath, path, sizeof(path));
    make_dirs(path);
}

static FILE *open_guest_output(const char *guestPath)
{
    while (*guestPath == ' ')
        ++guestPath;
    char path[MAX_PATH];
    host_path(guestPath, path, sizeof(path));
    return fopen(path, "wb");
}

static bool has_suffix(const char *name, const char *suffix)
{
    size_t n = strlen(name), s = strlen(suffix);
    return n >= s && strcmp(name + n - s, suffix) == 0;
}

static int write_ifconfig(const char *outputPath, int index, const unsigned char address[4],
                          const unsigned char mask[4], const unsigned char mac[6], int link)
{
    FILE *out = open_guest_output(outputPath);
    if (!out)
        return 1;
    fprintf(out, "%d", index);
    for (int i = 0; i < 4; ++i)
        fprintf(out, " %d", address[i]);
    for (int i = 0; i < 4; ++i)
        fprintf(out, " %d", mask[i]);
    for (int i = 0; i < 6; ++i)
        fprintf(out, " %d", mac[i]);
    fprintf(out, " %d\n", link);
    fclose(out);

    log_info(plfWmmt3, "Reported interface %d.%d.%d.%d to the cabinet", address[0], address[1], address[2], address[3]);
    return 0;
}

/* "$interface @address @mask @mac $link" */
static int answer_ifconfig(const char *outputPath)
{
    int index = 0, link = 0;
    unsigned char address[4] = {127, 0, 0, 1};
    unsigned char mask[4] = {255, 0, 0, 0};
    unsigned char mac[6] = {0};

    if (Config.wmmt3.linked_play)
    {
        /* As ifconfig.pl prints a cabinet whose eth0 has no address. */
        Wmmt3Interface nic = {.mask = {255, 255, 255, 0}};
        wmmt3_network_interface(&nic);
        return write_ifconfig(outputPath, index, nic.address, nic.mask, nic.mac, nic.link);
    }

    ULONG size = 0;
    GetAdaptersInfo(NULL, &size);
    IP_ADAPTER_INFO *adapters = size ? (IP_ADAPTER_INFO *)malloc(size) : NULL;
    if (adapters && GetAdaptersInfo(adapters, &size) == NO_ERROR)
    {
        for (IP_ADAPTER_INFO *a = adapters; a; a = a->Next)
        {
            unsigned long ip = inet_addr(a->IpAddressList.IpAddress.String);
            if (a->Type != MIB_IF_TYPE_ETHERNET && a->Type != IF_TYPE_IEEE80211)
                continue;
            if (ip == 0 || ip == INADDR_NONE)
                continue;

            unsigned long netmask = inet_addr(a->IpAddressList.IpMask.String);
            memcpy(address, &ip, 4);
            memcpy(mask, &netmask, 4);
            memcpy(mac, a->Address, a->AddressLength >= 6 ? 6 : a->AddressLength);
            link = 1;
            break;
        }
    }
    free(adapters);

    return write_ifconfig(outputPath, index, address, mask, mac, link);
}

static int answer_usbsize(const char *outputPath)
{
    ULARGE_INTEGER freeToCaller, total, totalFree;
    unsigned long long kilobytes = 0;
    if (GetDiskFreeSpaceExA(NULL, &freeToCaller, &total, &totalFree))
        kilobytes = total.QuadPart / 1024;

    FILE *out = open_guest_output(outputPath);
    if (!out)
        return 1;
    fprintf(out, "%llu\n", kilobytes);
    fclose(out);
    return 0;
}

static int answer_find(const char *command)
{
    static const struct
    {
        const char *prefix;
        const char *guestDir;
        const char *extension;
    } kinds[] = {
        {"find /tmp/data/target/", "/tmp/data/target", ".target.gz"},
        {"find data/target/jp", "data/target/jp", ".target.gz"},
        {"find data/target/us", "data/target/us", ".target.gz"},
        {"find /tmp/data/ranking/", "/tmp/data/ranking", ".rank"},
        {"find /tmp/data/maxicoin/", "/tmp/data/maxicoin", ".maxicoin"},
        {"find /tmp/data/joinstar/", "/tmp/data/joinstar", ".joinstar"},
    };

    for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); ++k)
    {
        if (strncmp(command, kinds[k].prefix, strlen(kinds[k].prefix)) != 0)
            continue;

        FILE *out = open_guest_output("/tmp/find.txt");
        if (!out)
            return 1;

        char dir[MAX_PATH], pattern[MAX_PATH * 2 + 16];
        host_path(kinds[k].guestDir, dir, sizeof(dir));
        snprintf(pattern, sizeof(pattern), "%s/*", dir);

        WIN32_FIND_DATAA data;
        HANDLE find = FindFirstFileA(pattern, &data);
        if (find != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    has_suffix(data.cFileName, kinds[k].extension))
                    fprintf(out, "%s/%s\n", kinds[k].guestDir, data.cFileName);
            } while (FindNextFileA(find, &data));
            FindClose(find);
        }
        fclose(out);
        return 0;
    }
    return -1;
}

static void copy_matching(const char *fromDir, const char *extension, const char *toGuestDir)
{
    char toDir[MAX_PATH], pattern[MAX_PATH * 2 + 16];
    host_path(toGuestDir, toDir, sizeof(toDir));
    make_dirs(toDir);
    snprintf(pattern, sizeof(pattern), "%s/*", fromDir);

    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do
    {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !has_suffix(data.cFileName, extension))
            continue;
        char from[MAX_PATH * 2 + 16], to[MAX_PATH * 2 + 16];
        snprintf(from, sizeof(from), "%s/%s", fromDir, data.cFileName);
        snprintf(to, sizeof(to), "%s/%s", toDir, data.cFileName);
        CopyFileA(from, to, FALSE);
    } while (FindNextFileA(find, &data));
    FindClose(find);
}

/* The production build leaves prepend-n2.pl to the start-up scripts. */
static int answer_prepend(void)
{
    static const char *dirs[] = {
        "/tmp/data/target", "/tmp/data/target/old", "/tmp/data/tournament", "/tmp/data/ranking",
        "/tmp/data/maxicoin", "/tmp/data/joinstar", "/tmp/data/card", "/tmp/data/etc",
        "/tmp/data2/tournament",
    };
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i)
        make_guest_dirs(dirs[i]);

    char to[MAX_PATH];
    host_path("/tmp/sys_04.wav", to, sizeof(to));
    CopyFileA("data/sound/bgm/maxi3/sys_04.wav", to, FALSE);
    host_path("/tmp/joinshot.png", to, sizeof(to));
    CopyFileA("data/sprite/Full_white.png", to, FALSE);

    log_info(plfWmmt3, "Prepared the work disk directories");
    return 0;
}

static int answer_privileged(const char *command)
{
    const char *cmd = command + 5; /* past "sudo " */
    const char *clock = strncmp(cmd, "busybox ", 8) == 0 ? cmd + 8 : cmd;

    if (strncmp(clock, "date ", 5) == 0 || strncmp(clock, "hwclock", 7) == 0)
        return 0;
    if (strncmp(cmd, "perl etc/usbsize.pl >", 21) == 0)
        return answer_usbsize(cmd + 21);
    if (strncmp(cmd, "ifconfig ", 9) == 0 || strncmp(cmd, "route ", 6) == 0)
        return 0;
    if (strncmp(cmd, "mkdir -p ", 9) == 0)
    {
        make_guest_dirs(cmd + 9);
        return 0;
    }
    if (strncmp(cmd, "umount ", 7) == 0)
        return 0;
    if (strncmp(cmd, "mount ", 6) == 0 || strncmp(cmd, "cp ", 3) == 0 || strncmp(cmd, "rm ", 3) == 0)
        return 1;

    log_warn(plfWmmt3, "Refused an unhandled privileged command: %s", command);
    return 1;
}

void wmmt3_prepare_work_disk(void)
{
    answer_prepend();
}

int wmmt3_system(const char *command)
{
    log_debug(plfWmmt3, "system(\"%s\")", command);

    if (strncmp(command, "perl etc/ifconfig.pl > ", 23) == 0)
        return answer_ifconfig(command + 23);
    if (strncmp(command, "perl prepend-n2.pl", 18) == 0)
        return answer_prepend();
    if (strncmp(command, "find ", 5) == 0 && strstr(command, ">/tmp/find.txt"))
    {
        int status = answer_find(command);
        if (status >= 0)
            return status;
    }
    if (strcmp(command, "cp -f data/target/*.target.gz /tmp/data/target/ 2>/dev/null") == 0)
    {
        copy_matching("data/target", ".target.gz", "/tmp/data/target");
        return 0;
    }
    if (strncmp(command, "stty ", 5) == 0)
        return 0;
    if (strncmp(command, "sudo ", 5) == 0)
        return answer_privileged(command);

    log_warn(plfWmmt3, "Refused an unhandled shell command: %s", command);
    return 1;
}
