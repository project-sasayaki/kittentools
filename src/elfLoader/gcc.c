#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <ctype.h>
#include <wctype.h>
#include <stdio.h>
#include "../log/log.h"
#include "elfLoader.h"
#include "gcc.h"
#include "symbol.h"


static unsigned short g_ctype_b[384]; /* -128 to 255 */
static const unsigned short *g_ctype_b_ptr = g_ctype_b + 128;

static int32_t g_ctype_tolower[384];
static const int32_t *g_ctype_tolower_ptr = g_ctype_tolower + 128;

static int32_t g_ctype_toupper[384];
static const int32_t *g_ctype_toupper_ptr = g_ctype_toupper + 128;

static int g_ctype_initialized = 0;

static void InitLinuxCtype(void);

void gccBridge_initBridges(void)
{
    symbol_register("__ctype_b_loc", __ctype_b_loc, NULL);
    symbol_register("__ctype_tolower_loc", __ctype_tolower_loc, NULL);
    symbol_register("__ctype_toupper_loc", __ctype_toupper_loc, NULL);
    symbol_register("__ctype_get_mb_cur_max", __ctype_get_mb_cur_max, NULL);

    symbol_register("__strtod_internal", __strtod_internal, NULL);
    symbol_register("__strtol_internal", __strtol_internal, NULL);
    symbol_register("__strtoul_internal", __strtoul_internal, NULL);

    symbol_register("__assert_fail", __assert_fail, NULL);
    symbol_register("__errno_location", __errno_location, NULL);
    symbol_register("__libc_freeres", __libc_freeres, NULL);

    symbol_register("tolower", tolower, NULL);
    symbol_register("toupper", toupper, NULL);
    symbol_register("towlower", towlower, NULL);
    symbol_register("towupper", towupper, NULL);
    symbol_register("iswctype", iswctype, NULL);
    symbol_register("wctype", wctype, NULL);
    InitLinuxCtype();
}

double __strtod_internal(const char *n, char **e, int g)
{
    return strtod(n, e);
}

long __strtol_internal(const char *n, char **e, int b, int g)
{
    return strtol(n, e, b);
}

unsigned long __strtoul_internal(const char *n, char **e, int b, int g)
{
    return strtoul(n, e, b);
}

const unsigned short **__ctype_b_loc(void)
{
    InitLinuxCtype();
    return &g_ctype_b_ptr;
}

const int32_t **__ctype_tolower_loc(void)
{
    InitLinuxCtype();
    return &g_ctype_tolower_ptr;
}

const int32_t **__ctype_toupper_loc(void)
{
    InitLinuxCtype();
    return &g_ctype_toupper_ptr;
}

size_t __ctype_get_mb_cur_max(void)
{
    return 1;
}

void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function)
{
    fprintf(stderr, "ASSERTION FAILED: %s at %s:%d (%s)\n", assertion, file, line, function);
    TerminateProcess(GetCurrentProcess(), 1);
}

void __libc_freeres(void)
{
}

int *__errno_location(void)
{
    return _errno();
}

static void InitLinuxCtype(void)
{
    if (g_ctype_initialized)
        return;

/* GLIBC internally utilizes byte-swapped layout for its short array on x86 little-endian */
#define GLIBC_IS_BIT(bit) ((unsigned short)((bit) < 8 ? ((1U << (bit)) << 8) : ((1U << (bit)) >> 8)))
    const unsigned short _ISupper = GLIBC_IS_BIT(0);
    const unsigned short _ISlower = GLIBC_IS_BIT(1);
    const unsigned short _ISalpha = GLIBC_IS_BIT(2);
    const unsigned short _ISdigit = GLIBC_IS_BIT(3);
    const unsigned short _ISxdigit = GLIBC_IS_BIT(4);
    const unsigned short _ISspace = GLIBC_IS_BIT(5);
    const unsigned short _ISprint = GLIBC_IS_BIT(6);
    const unsigned short _ISgraph = GLIBC_IS_BIT(7);
    const unsigned short _ISblank = GLIBC_IS_BIT(8);
    const unsigned short _IScntrl = GLIBC_IS_BIT(9);
    const unsigned short _ISpunct = GLIBC_IS_BIT(10);
    const unsigned short _ISalnum = GLIBC_IS_BIT(11);

    /* Initialize the ctype tables */
    for (int i = -128; i < 256; i++)
    {
        int c = i;
        unsigned short flags = 0;

        if (i >= -1 && i <= 255)
        {
            if (isupper(c))
                flags |= _ISupper | _ISalpha | _ISalnum | _ISprint | _ISgraph;
            if (islower(c))
                flags |= _ISlower | _ISalpha | _ISalnum | _ISprint | _ISgraph;
            if (isdigit(c))
                flags |= _ISdigit | _ISalnum | _ISprint | _ISgraph;
            if (isspace(c))
                flags |= _ISspace;
            if (isprint(c))
                flags |= _ISprint;
            if (isgraph(c))
                flags |= _ISgraph;
            if (iscntrl(c))
                flags |= _IScntrl;
            if (ispunct(c))
                flags |= _ISpunct | _ISprint | _ISgraph;
            if (isxdigit(c))
                flags |= _ISxdigit;
            if (c == ' ' || c == '\t')
                flags |= _ISblank;
        }

        g_ctype_b[128 + i] = flags;
        g_ctype_tolower[128 + i] = tolower(c);
        g_ctype_toupper[128 + i] = toupper(c);
    }
    g_ctype_initialized = 1;
    log_debug(plfBridges, "[GccBridge] Initialized Linux Ctype table.");
}
