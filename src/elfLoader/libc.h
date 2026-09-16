#pragma once

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <wchar.h>
#include <sys/types.h>
#include <windows.h>
#include <locale.h>

struct tm32
{
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    int32_t tm_gmtoff;
    uint32_t tm_zone;
};

struct linux_timespec
{
    int32_t tv_sec;
    int32_t tv_nsec;
};

struct linux_sigaction
{
    void (*sa_handler)(int);
    uint32_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

void libcBridge_initBridges(void);