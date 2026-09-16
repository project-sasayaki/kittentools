#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <process.h>
#include <sys/time.h>
#include <sys/utime.h>
#include <wctype.h>
#include <windows.h>
#include <wchar.h>
#include <locale.h>
#include "../hooks/file.h"
#include "libc.h"
#include "../log/log.h"
#include "symbol.h"
#include "elfLoader.h"

long long __divdi3(long long a, long long b);
unsigned long long __udivdi3(unsigned long long a, unsigned long long b);
unsigned long long __umoddi3(unsigned long long a, unsigned long long b);
unsigned long long __fixunsdfdi(double a);
unsigned long long __fixunssfdi(float a);

struct linux_utimbuf;

int hook_cxa_atexit(void (*func)(void *), void *arg, void *dso_handle);

int hook_printf(const char *format, ...);
int hook_fprintf(void *stream, const char *format, ...);

int hook_snprintf(char *buffer, size_t count, const char *format, ...);
int hook_vfprintf(void *stream, const char *format, va_list args);
int hook_fscanf(FILE *stream, const char *format, ...);

int32_t hook_time(int32_t *tloc);
int hook_utime(const char *filename, const struct linux_utimbuf *times);
int hook_gettimeofday(struct timeval *tv, void *tz);
int hook_settimeofday(const struct timeval *tv, const void *tz);
int hook_usleep(uint32_t microseconds);
int hook_sleep(uint32_t seconds);
int hook_nanosleep(const struct timespec *req, struct timespec *rem);
struct tm32 *hook_localtime(const int32_t *timer);
char *hook_ctime_r(const time_t *clock, char *buf);

int hook_fork(void);
int hook_daemon(int nochdir, int noclose);
int hook_execlp(const char *file, const char *arg, ...);
int hook_kill(int pid, int sig);
int hook_wait(int *wstatus);
int hook_raise(int sig);

int hook_poll(struct pollfd *fds, int nfds, int timeout);

int hook_wmemcmp(const uint32_t *s1, const uint32_t *s2, size_t n);
uint32_t *hook_wmemcpy(uint32_t *dest, const uint32_t *src, size_t n);
uint32_t *hook_wmemset(uint32_t *s, uint32_t c, size_t n);
uint32_t *hook_wmemchr(const uint32_t *s, uint32_t c, size_t n);
size_t hook_wcslen(const uint32_t *s);
int hook_wcscoll(const uint32_t *s1, const uint32_t *s2);
int hook_wctob(uint32_t c);
size_t hook_wcrtomb(char *s, uint32_t wc, void *ps);
size_t hook_wcsftime(uint32_t *s, size_t maxsize, const uint32_t *format, const struct tm *timeptr);
size_t hook_wcsxfrm(uint32_t *dst, const uint32_t *src, size_t n);
size_t hook_mbrtowc(uint32_t *pwc, const char *s, size_t n, void *ps);
uint32_t hook_btowc(int c);
uint32_t hook_putwc(uint32_t wc, FILE *stream);
uint32_t hook_getwc(FILE *stream);
uint32_t hook_ungetwc(uint32_t wc, FILE *stream);

FILE *hook_popen(const char *command, const char *type);
int hook_pclose(FILE *stream);
void hook_perror(const char *s);
char *hook_realpath(const char *path, char *resolved_path);

char *hook_getenv(const char *name);
struct tm *hook_localtime_r(const time_t *timep, struct tm *result);

void *hook_dlopen(const char *filename, int flags);
void *hook_dlsym(void *handle, const char *symbol);
int hook_dlclose(void *handle);
char *hook_dlerror();

int hook_rand_r(unsigned int *seedp);

static FILE *g_stdin = NULL;
static FILE *g_stdout = NULL;
static FILE *g_stderr = NULL;

void libcBridge_initBridges(void)
{
    log_debug(plfBridges, "Initializing Libc Bridges...");

    g_stdin = stdin;
    g_stdout = stdout;
    g_stderr = stderr;

    // printf family
    symbol_register("printf", hook_printf, NULL);
    symbol_register("puts", puts, NULL);
    symbol_register("fprintf", hook_fprintf, NULL);
    symbol_register("sprintf", sprintf, NULL);
    symbol_register("snprintf", hook_snprintf, NULL);
    symbol_register("vfprintf", hook_vfprintf, NULL);
    symbol_register("vsprintf", vsprintf, NULL);
    symbol_register("vsnprintf", vsnprintf, NULL);
    symbol_register("fscanf", hook_fscanf, NULL);
    symbol_register("sscanf", sscanf, NULL);
    symbol_register("index", strchr, NULL);

    symbol_register("time", hook_time, NULL);
    symbol_register("gettimeofday", hook_gettimeofday, NULL);
    symbol_register("settimeofday", hook_settimeofday, NULL);
    symbol_register("localtime", hook_localtime, NULL);
    symbol_register("utime", hook_utime, NULL);
    symbol_register("usleep", hook_usleep, NULL);
    symbol_register("sleep", hook_sleep, NULL);
    symbol_register("nanosleep", hook_nanosleep, NULL);
    symbol_register("localtime_r", hook_localtime_r, NULL);
    symbol_register("mktime", mktime, NULL);
    symbol_register("strftime", strftime, NULL);
    symbol_register("ctime_r", hook_ctime_r, NULL);

    symbol_register("abort", abort, NULL);
    symbol_register("exit", exit, NULL);
    symbol_register("_exit", exit, NULL);

    symbol_register("__cxa_atexit", hook_cxa_atexit, NULL);

    symbol_register("setlocale", setlocale, NULL);

    symbol_register("stdin", &g_stdin, NULL);
    symbol_register("stdout", &g_stdout, NULL);
    symbol_register("stderr", &g_stderr, NULL);

    symbol_register("__divdi3", __divdi3, NULL);
    symbol_register("__udivdi3", __udivdi3, NULL);
    symbol_register("__umoddi3", __umoddi3, NULL);
    symbol_register("__fixunsdfdi", __fixunsdfdi, NULL);
    symbol_register("__fixunssfdi", __fixunssfdi, NULL);

    symbol_register("system", system, NULL);

    symbol_register("getpid", _getpid, NULL);
    symbol_register("fork", hook_fork, NULL);
    symbol_register("daemon", hook_daemon, NULL);
    symbol_register("execlp", hook_execlp, NULL);
    symbol_register("kill", hook_kill, NULL);
    symbol_register("wait", hook_wait, NULL);
    symbol_register("getenv", hook_getenv, NULL);

    symbol_register("rand", rand, NULL);
    symbol_register("rand_r", hook_rand_r, NULL);
    symbol_register("srand", srand, NULL);
    symbol_register("raise", hook_raise, NULL);
    symbol_register("poll", hook_poll, NULL);
    symbol_register("realpath", hook_realpath, NULL);
    symbol_register("popen", hook_popen, NULL);
    symbol_register("pclose", hook_pclose, NULL);
    symbol_register("perror", hook_perror, NULL);

    symbol_register("atoi", atoi, NULL);

    symbol_register("iopl", hook_ret_zero, NULL);

    symbol_register("dlopen", hook_dlopen, NULL);
    symbol_register("dlsym", hook_dlsym, NULL);
    symbol_register("dlclose", hook_dlclose, NULL);
    symbol_register("dlerror", hook_dlerror, NULL);

    symbol_register("kswap_collect", hook_ret_zero, NULL);

    symbol_register("wmemcmp", hook_wmemcmp, NULL);
    symbol_register("wmemcpy", hook_wmemcpy, NULL);
    symbol_register("wmemset", hook_wmemset, NULL);
    symbol_register("wmemchr", hook_wmemchr, NULL);
    symbol_register("wcslen", hook_wcslen, NULL);
    symbol_register("wcscoll", hook_wcscoll, NULL);
    symbol_register("wctob", hook_wctob, NULL);
    symbol_register("wctype", wctype, NULL);
    symbol_register("wcrtomb", hook_wcrtomb, NULL);
    symbol_register("wcsftime", hook_wcsftime, NULL);
    symbol_register("wcsxfrm", hook_wcsxfrm, NULL);
    symbol_register("mbrtowc", hook_mbrtowc, NULL);
    symbol_register("btowc", hook_btowc, NULL);
    symbol_register("putwc", hook_putwc, NULL);
    symbol_register("getwc", hook_getwc, NULL);
    symbol_register("ungetwc", hook_ungetwc, NULL);
}

int hook_cxa_atexit(void (*func)(void *), void *arg, void *dso_handle)
{
    log_debug(plfBridges, "__cxa_atexit called");
    return 0;
}

int hook_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char msg[1024];

    int ret = vsnprintf(msg, sizeof(msg), format, args);
    log_game(plfPrintf, msg);

    va_end(args);
    return ret;
}

int hook_fprintf(void *stream, const char *format, ...)
{
    va_list args;
    int ret = 0;
    va_start(args, format);
    char msg[1024];

    if (stream != stdout && stream != stderr)
        ret = vfprintf((FILE *)stream, format, args);
    else
    {
        ret = vsnprintf(msg, sizeof(msg), format, args);
        log_game(plfFprintf, msg);
    }
    va_end(args);
    return ret;
}

int hook_snprintf(char *buffer, size_t count, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    if (format == NULL)
        return 0;
    int ret = _vsnprintf(buffer, count, format, args);
    va_end(args);
    return ret;
}

int hook_vfprintf(void *stream, const char *format, va_list args)
{
    if (stream == NULL)
    {
        stream = stdout;
    }
    return vfprintf((FILE *)stream, format, args);
}

int hook_fscanf(FILE *stream, const char *format, ...)
{
    if (!stream)
        return 0;

    va_list args;
    va_start(args, format);
    int ret = 0;
    ret = vfscanf(stream, format, args);
    va_end(args);
    return ret;
}

int32_t hook_time(int32_t *tloc)
{
    time_t t = time(NULL);
    if (tloc)
    {
        *tloc = (int32_t)t;
    }
    return (int32_t)t;
}

int hook_utime(const char *filename, const struct linux_utimbuf *times)
{
    char winPath[MAX_PATH];
    getKittenPath(filename, winPath);
    return _utime(winPath, (struct _utimbuf *)times);
}

int hook_gettimeofday(struct timeval *tv, void *tz)
{
    return gettimeofday(tv, tz);
}

int hook_settimeofday(const struct timeval *tv, const void *tz)
{
    return 0;
}

int hook_usleep(uint32_t microseconds)
{
    log_trace(plfBridges, "Intercepted usleep");
    Sleep(microseconds / 1000);
    return 0;
}

int hook_sleep(uint32_t seconds)
{
    log_trace(plfBridges, "Intercepted sleep");
    Sleep(seconds * 1000);
    return 0;
}

int hook_nanosleep(const struct timespec *req, struct timespec *rem)
{
    long long duration_ns = (long long)req->tv_sec * 1000000000LL + req->tv_nsec;
    if (duration_ns <= 0)
        return 0;

    long long freq;
    QueryPerformanceFrequency((LARGE_INTEGER *)&freq);
    long long wait_ticks = (duration_ns * freq) / 1000000000LL;

    long long start, current;
    QueryPerformanceCounter((LARGE_INTEGER *)&start);

    while (1)
    {
        QueryPerformanceCounter((LARGE_INTEGER *)&current);
        long long elapsed = current - start;
        if (elapsed >= wait_ticks)
            break;

        long long remaining_us = ((wait_ticks - elapsed) * 1000000LL) / freq;

        if (remaining_us > 2000)
        {
            Sleep(1);
        }
        else if (remaining_us > 100)
        {
            Sleep(0);
        }
    }

    return 0;
}

struct tm32 *hook_localtime(const int32_t *timer)
{
    static struct tm32 t32;
    time_t t = (time_t)*timer;
    struct tm *tm_ptr = localtime(&t);

    if (tm_ptr)
    {
        t32.tm_sec = tm_ptr->tm_sec;
        t32.tm_min = tm_ptr->tm_min;
        t32.tm_hour = tm_ptr->tm_hour;
        t32.tm_mday = tm_ptr->tm_mday;
        t32.tm_mon = tm_ptr->tm_mon;
        t32.tm_year = tm_ptr->tm_year;
        t32.tm_wday = tm_ptr->tm_wday;
        t32.tm_yday = tm_ptr->tm_yday;
        t32.tm_isdst = tm_ptr->tm_isdst;
        t32.tm_gmtoff = 0;
        t32.tm_zone = 0;
        return &t32;
    }
    return NULL;
}

char *hook_ctime_r(const time_t *clock, char *buf) {
    struct tm tm_buf;
    struct tm *tm_info = NULL;

    if (clock == NULL || buf == NULL) {
        return NULL;
    }

    if (localtime_s(&tm_buf, clock) != 0) {
        return NULL;
    }
    tm_info = &tm_buf;

    if (strftime(buf, 26, "%a %b %d %H:%M:%S %Y\n", tm_info) == 0) {
        return NULL;
    }

    return buf;
}

int hook_fork(void)
{
    log_debug(plfBridges, "fork() called - returning fake parent PID 1000");
    return 1000;
}

int hook_daemon(int nochdir, int noclose)
{
    log_debug(plfBridges, "daemon(%d, %d) called - stubbed success", nochdir, noclose);
    return 0;
}

int hook_execlp(const char *file, const char *arg, ...)
{
    log_debug(plfBridges, "execlp(\"%s\", \"%s\", ...)", file, arg);
    return 0;
}

int hook_raise(int sig)
{
    log_debug(plfBridges, "raise(%d) called", sig);
    int win_sig = -1;
    switch (sig)
    {
        case 2: // SIGINT
            win_sig = SIGINT;
            break;
        case 5: // SIGTRAP
            // Windows doesn't have a direct equivalent in csignal, mapping to SIGABRT
            win_sig = SIGABRT;
            break;
        case 15: // SIGTERM
            win_sig = SIGTERM;
            break;
        case 6: // SIGABRT
            win_sig = SIGABRT;
            break;
        case 8: // SIGFPE
            win_sig = SIGFPE;
            break;
        case 4: // SIGILL
            win_sig = SIGILL;
            break;
        case 11: // SIGSEGV
            win_sig = SIGSEGV;
            break;
        default:
            log_warn(plfBridges, "raise: unsupported signal %d", sig);
            return -1;
    }
    return raise(win_sig);
}

int hook_kill(int pid, int sig)
{
    int my_pid = _getpid();
    log_debug(plfBridges, "kill(%d, %d) called", pid, sig);

    if (pid == my_pid || pid == 0)
    {
        // Signal 0 is existence check
        if (sig == 0)
            return 0;

        // SIGKILL (9) or SIGTERM (15) -> Terminate self
        if (sig == 9 || sig == 15)
        {
            log_warn(plfBridges, "kill: Process requested self-termination");
            exit(0);
        }
    }

    return 0;
}

int hook_wait(int *wstatus)
{
    log_warn(plfBridges, "wait() called: No child processes to wait for. Returning ECHILD.");

    Sleep(10);

    if (wstatus)
        *wstatus = 0;
    errno = ECHILD;
    return -1;
}

int hook_poll(struct pollfd *fds, int nfds, int timeout)
{
    log_trace(plfBridges, "Intercepted poll");

    for (int i = 0; i < nfds; i++)
    {
        fds[i].revents = 0;
    }
    return 0;
}

int hook_wmemcmp(const uint32_t *s1, const uint32_t *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] < s2[i]) return -1;
        if (s1[i] > s2[i]) return 1;
    }
    return 0;
}

uint32_t *hook_wmemcpy(uint32_t *dest, const uint32_t *src, size_t n) {
    for (size_t i = 0; i < n; i++) dest[i] = src[i];
    return dest;
}

uint32_t *hook_wmemset(uint32_t *s, uint32_t c, size_t n) {
    for (size_t i = 0; i < n; i++) s[i] = c;
    return s;
}

uint32_t *hook_wmemchr(const uint32_t *s, uint32_t c, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == c) return (uint32_t*)(s + i);
    }
    return NULL;
}

size_t hook_wcslen(const uint32_t *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

int hook_wcscoll(const uint32_t *s1, const uint32_t *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++; s2++;
    }
    return (*s1 > *s2) - (*s1 < *s2);
}

int hook_wctob(uint32_t c) {
    return (c < 128) ? (int)c : -1;
}

size_t hook_wcrtomb(char *s, uint32_t wc, void *ps) {
    if (!s) return 1;
    if (wc > 255) { errno = EILSEQ; return (size_t)-1; }
    *s = (char)wc;
    return 1;
}

size_t hook_wcsftime(uint32_t *s, size_t maxsize, const uint32_t *format, const struct tm *timeptr) {
    char fmt[256];
    char out[256];
    size_t i = 0;
    while (format[i] && i < 255) { fmt[i] = (char)format[i]; i++; }
    fmt[i] = 0;
    size_t res = strftime(out, maxsize, fmt, timeptr);
    for (i = 0; i < res; i++) s[i] = (uint32_t)out[i];
    if (maxsize > res) s[res] = 0;
    return res;
}

size_t hook_wcsxfrm(uint32_t *dst, const uint32_t *src, size_t n) {
    size_t i = 0;
    while (src[i] && i < n) { if (dst) dst[i] = src[i]; i++; }
    if (dst && i < n) dst[i] = 0;
    return hook_wcslen(src);
}

size_t hook_mbrtowc(uint32_t *pwc, const char *s, size_t n, void *ps) {
    if (!s) return 0;
    if (n == 0) return (size_t)-2;
    if (pwc) *pwc = (uint32_t)(unsigned char)*s;
    return (*s == 0) ? 0 : 1;
}

uint32_t hook_btowc(int c) {
    if (c == EOF) return (uint32_t)-1;
    return (uint32_t)(unsigned char)c;
}

uint32_t hook_putwc(uint32_t wc, FILE *stream) {
    int r = fputc((int)(wc & 0xFF), stream);
    return (r == EOF) ? (uint32_t)-1 : wc;
}

uint32_t hook_getwc(FILE *stream) {
    int c = fgetc(stream);
    return (c == EOF) ? (uint32_t)-1 : (uint32_t)c;
}

uint32_t hook_ungetwc(uint32_t wc, FILE *stream) {
    int r = ungetc((int)(wc & 0xFF), stream);
    return (r == EOF) ? (uint32_t)-1 : wc;
}

FILE *hook_popen(const char *command, const char *type)
{
    log_debug(plfBridges, "Intercepted popen: %s %s", command, type);
    return NULL;
}

int hook_pclose(FILE *stream)
{
    log_debug(plfBridges, "Intercepted pclose: %p", stream);
    return -1;
}

void hook_perror(const char *s)
{
}

char *hook_realpath(const char *path, char *resolved_path)
{
    if (!path || !resolved_path)
        return NULL;

    char pidExe[64];
    _snprintf(pidExe, sizeof(pidExe), "/proc/%u/exe", _getpid());

    if (!strcmp(path, "/proc/self/exe") || !strcmp(path, "/proc/thread-self/exe") || !strcmp(path, pidExe))
    {
        char modulePath[MAX_PATH];
        if (!GetModuleFileNameA(NULL, modulePath, MAX_PATH))
            return NULL;

        if (!_fullpath(resolved_path, modulePath, MAX_PATH))
            return NULL;

        for (char *p = resolved_path; *p; p++)
        {
            if (*p == '\\')
                *p = '/';
        }

        return resolved_path;
    }

    /* We should handle other situations, but this is enough for IDAS for now. */
    return NULL;
}

char *hook_getenv(const char *name)
{
    if (strcmp(name, "TEA_DIR") == 0)
    {
        return ".";
    }

    if (strcmp(name, "__GL_SYNC_TO_VBLANK") == 0)
    {
        return "";
    }
    return getenv(name);
}

struct tm *hook_localtime_r(const time_t *timep, struct tm *result)
{
    const int32_t *t32 = (const int32_t *)timep;
    time_t t = (time_t)*t32;

    if (localtime_s(result, &t) == 0)
        return result;
    return NULL;
}

void *hook_dlopen(const char *filename, int flags)
{
    if (!filename)
        return (void *)-1; // RTLD_DEFAULT handle

    hook_load_needed_library(filename);

    return (void *)0xDEADBEEF;
}

void *hook_dlsym(void *handle, const char *symbol)
{
    void *func = hook_resolve_symbol(symbol);
    return func;
}

int hook_dlclose(void *handle)
{
    return 0;
}

char *hook_dlerror()
{
    return NULL;
}

int hook_rand_r(unsigned int *seedp)
{
    *seedp = *seedp * 1103515245u + 12345u;
    return (int)(*seedp / 65536u % 32768u);
}
