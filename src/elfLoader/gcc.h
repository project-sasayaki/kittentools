#pragma once


#include <stdint.h>
#include <stddef.h>

void gccBridge_initBridges(void);

double __strtod_internal(const char *n, char **e, int g);
long __strtol_internal(const char *n, char **e, int b, int g);
unsigned long __strtoul_internal(const char *n, char **e, int b, int g);
const unsigned short **__ctype_b_loc(void);
const int32_t **__ctype_tolower_loc(void);
const int32_t **__ctype_toupper_loc(void);
size_t __ctype_get_mb_cur_max(void);

void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function);
int *__errno_location(void);
void __libc_freeres(void);
