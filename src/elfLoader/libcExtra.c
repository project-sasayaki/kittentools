#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ctype.h>
#include <errno.h>
#include <io.h>
#include <math.h>
#include <process.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wctype.h>

#include "libcExtra.h"
#include "libc.h"
#include "file.h"
#include "elfLoader.h"
#include "symbol.h"
#include "../devices/device.h"
#include "../log/log.h"

/* glibc's i386 jmp_buf; no pointer mangling, only we read it. */
int kt_setjmp(void *env);
int kt_sigsetjmp(void *env, int saveMask);
void kt_longjmp(void *env, int value);

__asm__(
    ".text\n"
    ".globl _kt_setjmp\n"
    ".globl _kt_sigsetjmp\n"
    "_kt_setjmp:\n"
    "_kt_sigsetjmp:\n"
    "    movl 4(%esp), %eax\n"
    "    movl %ebx, 0(%eax)\n"
    "    movl %esi, 4(%eax)\n"
    "    movl %edi, 8(%eax)\n"
    "    movl %ebp, 12(%eax)\n"
    "    leal 4(%esp), %ecx\n"
    "    movl %ecx, 16(%eax)\n"
    "    movl (%esp), %ecx\n"
    "    movl %ecx, 20(%eax)\n"
    "    movl $0, 24(%eax)\n"
    "    xorl %eax, %eax\n"
    "    ret\n"
    ".globl _kt_longjmp\n"
    "_kt_longjmp:\n"
    "    movl 4(%esp), %edx\n"
    "    movl 8(%esp), %eax\n"
    "    testl %eax, %eax\n"
    "    jnz 1f\n"
    "    incl %eax\n"
    "1:\n"
    "    movl 0(%edx), %ebx\n"
    "    movl 4(%edx), %esi\n"
    "    movl 8(%edx), %edi\n"
    "    movl 12(%edx), %ebp\n"
    "    movl 16(%edx), %esp\n"
    "    jmp *20(%edx)\n");

/* Leaves no frame of ours for the unwinder. */
void *g_realCxaThrow;
void kt_log_throw(const void *typeInfo, const void *caller);
void kt_cxa_throw(void *object, void *typeInfo, void (*destructor)(void *));

__asm__(
    ".text\n"
    ".globl _kt_cxa_throw\n"
    "_kt_cxa_throw:\n"
    "    pushl (%esp)\n"          /* caller */
    "    pushl 12(%esp)\n"        /* type_info */
    "    call _kt_log_throw\n"
    "    addl $8, %esp\n"
    "    jmp *_g_realCxaThrow\n");

void kt_log_throw(const void *typeInfo, const void *caller)
{
    if (!g_realCxaThrow)
        g_realCxaThrow = elf_lookup_guest_library_symbol("__cxa_throw");
    /* vtable, then the mangled name */
    const char *name = typeInfo ? *(const char *const *)((const uint8_t *)typeInfo + 4) : "?";
    log_debug(plfBridges, "C++ throw %s from %p", name ? name : "?", caller);
}

void libcExtra_trace_cxx_throws(void)
{
    symbol_register("__cxa_throw", kt_cxa_throw, NULL);
}

#define CTYPE_WRAPPER(name)                                                    \
    static int kt_##name(int c) { return (c >= 0 && c < 256) ? name(c) : 0; }
CTYPE_WRAPPER(isalnum)
CTYPE_WRAPPER(isalpha)
CTYPE_WRAPPER(iscntrl)
CTYPE_WRAPPER(islower)
CTYPE_WRAPPER(isprint)
CTYPE_WRAPPER(ispunct)
CTYPE_WRAPPER(isspace)
CTYPE_WRAPPER(isupper)
CTYPE_WRAPPER(isxdigit)

#define WCTYPE_WRAPPER(name)                                                   \
    static int kt_##name(uint32_t c) { return c < 0x10000 ? name((wint_t)c) : 0; }
WCTYPE_WRAPPER(iswalpha)
WCTYPE_WRAPPER(iswcntrl)
WCTYPE_WRAPPER(iswdigit)
WCTYPE_WRAPPER(iswlower)
WCTYPE_WRAPPER(iswprint)
WCTYPE_WRAPPER(iswpunct)
WCTYPE_WRAPPER(iswspace)
WCTYPE_WRAPPER(iswupper)
WCTYPE_WRAPPER(iswxdigit)

static int kt_wcscmp(const uint32_t *a, const uint32_t *b)
{
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return *a < *b ? -1 : (*a > *b ? 1 : 0);
}

static int kt_wcsncmp(const uint32_t *a, const uint32_t *b, size_t n)
{
    for (; n; --n, ++a, ++b)
    {
        if (*a != *b)
            return *a < *b ? -1 : 1;
        if (!*a)
            break;
    }
    return 0;
}

static uint32_t *kt_wcscpy(uint32_t *dst, const uint32_t *src)
{
    uint32_t *out = dst;
    while ((*dst++ = *src++) != 0)
        ;
    return out;
}

static long kt_wcstol_internal(const uint32_t *s, uint32_t **end, int base, int group)
{
    (void)group;
    char narrow[128];
    size_t n = 0;
    while (s[n] && s[n] < 0x80 && n < sizeof(narrow) - 1)
    {
        narrow[n] = (char)s[n];
        ++n;
    }
    narrow[n] = '\0';

    char *narrowEnd = NULL;
    long value = strtol(narrow, &narrowEnd, base);
    if (end)
        *end = (uint32_t *)(s + (narrowEnd - narrow));
    return value;
}

static int kt_swprintf(uint32_t *dst, size_t count, const uint32_t *format, ...)
{
    char narrowFormat[512];
    size_t n = 0;
    while (format[n] && n < sizeof(narrowFormat) - 1)
    {
        narrowFormat[n] = format[n] < 0x80 ? (char)format[n] : '?';
        ++n;
    }
    narrowFormat[n] = '\0';

    char narrow[1024];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(narrow, sizeof(narrow), narrowFormat, args);
    va_end(args);
    if (written < 0 || count == 0)
        return -1;

    size_t i = 0;
    for (; narrow[i] && i < count - 1; ++i)
        dst[i] = (unsigned char)narrow[i];
    dst[i] = 0;
    return (size_t)written < count ? written : -1;
}

extern long long __moddi3(long long a, long long b);

static float kt_strtof_internal(const char *s, char **end, int group)
{
    (void)group;
    return strtof(s, end);
}

static long double kt_strtold_internal(const char *s, char **end, int group)
{
    (void)group;
    return strtold(s, end);
}

static float kt_fabsf(float x) { return fabsf(x); }
static float kt_ceilf(float x) { return ceilf(x); }
static long kt_lrint(double x) { return lrint(x); }
static int kt_isdigit(int c) { return c >= '0' && c <= '9'; }
static double kt_ldexp(double x, int e) { return ldexp(x, e); }
static double kt_log2(double x) { return log2(x); }
static int kt_isinf(double x) { return isinf(x) ? (x > 0 ? 1 : -1) : 0; }
static int kt_isnan(double x) { return isnan(x) ? 1 : 0; }

static long kt_random(void)
{
    return ((long)rand() << 16 ^ (long)rand()) & 0x7fffffff;
}

static void kt_bzero(void *p, size_t n) { memset(p, 0, n); }

static char *kt_strerror_r(int errnum, char *buf, size_t len)
{
    /* glibc exports the GNU strerror_r. */
    if (buf && len)
    {
        strncpy(buf, strerror(errnum), len - 1);
        buf[len - 1] = '\0';
    }
    return buf;
}

/* glibc i386: ten strings, then fourteen chars */
struct linux_lconv
{
    char *decimal_point, *thousands_sep, *grouping, *int_curr_symbol, *currency_symbol;
    char *mon_decimal_point, *mon_thousands_sep, *mon_grouping, *positive_sign, *negative_sign;
    char chars[14];
};

static struct linux_lconv *kt_localeconv(void)
{
    static struct linux_lconv lc;
    static bool ready = false;
    if (!ready)
    {
        lc.decimal_point = ".";
        lc.thousands_sep = lc.grouping = lc.int_curr_symbol = lc.currency_symbol = "";
        lc.mon_decimal_point = lc.mon_thousands_sep = lc.mon_grouping = "";
        lc.positive_sign = lc.negative_sign = "";
        memset(lc.chars, 127 /* CHAR_MAX */, sizeof(lc.chars));
        ready = true;
    }
    return &lc;
}

static const char *kt_gnu_get_libc_version(void) { return "2.3.6"; }

static int64_t qpc_now(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

static int64_t qpc_frequency(void)
{
    static LARGE_INTEGER frequency;
    if (frequency.QuadPart == 0)
        QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart;
}

static struct tm32 *kt_gmtime_r(const int32_t *timep, struct tm32 *out)
{
    __time64_t t = *timep;
    struct tm tm;
    if (!timep || !out || _gmtime64_s(&tm, &t) != 0)
        return NULL;
    out->tm_sec = tm.tm_sec;
    out->tm_min = tm.tm_min;
    out->tm_hour = tm.tm_hour;
    out->tm_mday = tm.tm_mday;
    out->tm_mon = tm.tm_mon;
    out->tm_year = tm.tm_year;
    out->tm_wday = tm.tm_wday;
    out->tm_yday = tm.tm_yday;
    out->tm_isdst = 0;
    out->tm_gmtoff = 0;
    out->tm_zone = (uint32_t)(uintptr_t)"GMT";
    return out;
}

static struct tm32 *kt_gmtime(const int32_t *timep)
{
    static struct tm32 result;
    return kt_gmtime_r(timep, &result);
}

static int32_t kt_timegm(struct tm32 *in)
{
    struct tm tm = {0};
    tm.tm_sec = in->tm_sec;
    tm.tm_min = in->tm_min;
    tm.tm_hour = in->tm_hour;
    tm.tm_mday = in->tm_mday;
    tm.tm_mon = in->tm_mon;
    tm.tm_year = in->tm_year;
    return (int32_t)_mkgmtime64(&tm);
}

static uint64_t filetime_to_100ns(FILETIME ft)
{
    return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static int kt_clock_gettime(int clockId, struct linux_timespec *ts)
{
    if (!ts)
        return -1;

    switch (clockId)
    {
        case 0: /* CLOCK_REALTIME */
        {
            FILETIME ft;
            GetSystemTimePreciseAsFileTime(&ft);
            uint64_t t = filetime_to_100ns(ft) - 116444736000000000ULL;
            ts->tv_sec = (int32_t)(t / 10000000ULL) + libc_wall_clock_offset();
            ts->tv_nsec = (int32_t)((t % 10000000ULL) * 100);
            return 0;
        }
        case 2: /* CLOCK_PROCESS_CPUTIME_ID */
        case 3: /* CLOCK_THREAD_CPUTIME_ID */
        {
            FILETIME created, exited, kernel, user;
            BOOL ok = clockId == 2 ? GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)
                                   : GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
            uint64_t t = ok ? filetime_to_100ns(kernel) + filetime_to_100ns(user) : 0;
            ts->tv_sec = (int32_t)(t / 10000000ULL);
            ts->tv_nsec = (int32_t)((t % 10000000ULL) * 100);
            return 0;
        }
        default: /* MONOTONIC, MONOTONIC_RAW, BOOTTIME, ... */
        {
            int64_t now = qpc_now(), frequency = qpc_frequency();
            ts->tv_sec = (int32_t)(now / frequency);
            ts->tv_nsec = (int32_t)((now % frequency) * 1000000000LL / frequency);
            return 0;
        }
    }
}

/* Linux CLOCKS_PER_SEC is 1000000. */
static long kt_clock(void)
{
    struct linux_timespec ts;
    kt_clock_gettime(2, &ts);
    return (long)ts.tv_sec * 1000000L + ts.tv_nsec / 1000;
}

static unsigned int kt_ret_root(void) { return 0; }
static int kt_getppid(void) { return 1; }
static int kt_setsid(void) { return _getpid(); }
static int kt_getpagesize(void) { return 4096; }

static int kt_get_nprocs(void)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (int)info.dwNumberOfProcessors;
}

static long kt_sysconf(int name)
{
    switch (name)
    {
        case 2:  /* _SC_CLK_TCK */
            return 100;
        case 4:  /* _SC_OPEN_MAX */
            return 1024;
        case 30: /* _SC_PAGESIZE */
            return 4096;
        case 69: /* _SC_GETGR_R_SIZE_MAX */
        case 70: /* _SC_GETPW_R_SIZE_MAX */
            return 1024;
        case 83: /* _SC_NPROCESSORS_CONF */
        case 84: /* _SC_NPROCESSORS_ONLN */
            return kt_get_nprocs();
        case 85: /* _SC_PHYS_PAGES */
        case 86: /* _SC_AVPHYS_PAGES */
        {
            MEMORYSTATUSEX status = {.dwLength = sizeof(status)};
            GlobalMemoryStatusEx(&status);
            uint64_t bytes = name == 85 ? status.ullTotalPhys : status.ullAvailPhys;
            uint64_t pages = bytes / 4096;
            return pages > 0x7fffffff ? 0x7fffffff : (long)pages;
        }
        default:
            errno = EINVAL;
            return -1;
    }
}

struct linux_passwd
{
    char *pw_name;
    char *pw_passwd;
    uint32_t pw_uid;
    uint32_t pw_gid;
    char *pw_gecos;
    char *pw_dir;
    char *pw_shell;
};

static int kt_getpwuid_r(uint32_t uid, struct linux_passwd *pwd, char *buf, size_t buflen,
                         struct linux_passwd **result)
{
    static const char strings[] = "root\0x\0/root\0/bin/sh";
    if (!pwd || !buf || buflen < sizeof(strings))
    {
        if (result)
            *result = NULL;
        return ERANGE;
    }
    memcpy(buf, strings, sizeof(strings));
    pwd->pw_name = buf;
    pwd->pw_passwd = buf + 5;
    pwd->pw_gecos = buf;
    pwd->pw_dir = buf + 7;
    pwd->pw_shell = buf + 13;
    pwd->pw_uid = uid;
    pwd->pw_gid = 0;
    if (result)
        *result = pwd;
    return 0;
}

static int kt_execl(const char *path, const char *arg, ...)
{
    (void)arg;
    log_warn(plfBridges, "execl(%s) is not supported", path ? path : "(null)");
    errno = ENOENT;
    return -1;
}

/* A fake child that exits successfully at once. */
static int kt_waitpid(int pid, int *status, int options)
{
    (void)options;
    if (status)
        *status = 0;
    return pid > 0 ? pid : 1000;
}

static int kt_putenv(char *string) { return _putenv(string); }

static int kt_setenv(const char *name, const char *value, int overwrite)
{
    if (!overwrite && getenv(name))
        return 0;
    return _putenv_s(name, value) == 0 ? 0 : -1;
}

static void kt_openlog(const char *ident, int option, int facility)
{
    (void)ident;
    (void)option;
    (void)facility;
}

static void kt_syslog(int priority, const char *format, ...)
{
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    log_game(plfBridges, "syslog(%d): %s", priority, message);
}

/* POSIX AIO, done synchronously. */
struct linux_aiocb
{
    int aio_fildes;
    int aio_lio_opcode;
    int aio_reqprio;
    void *aio_buf;
    size_t aio_nbytes;
    uint8_t aio_sigevent[64];
    void *next_prio;
    int abs_prio;
    int policy;
    int error_code;
    int return_value;
    int32_t aio_offset;
    uint8_t pad[4 + 32];
};

static int aio_transfer(struct linux_aiocb *cb, bool write)
{
    int64_t saved = _lseeki64(cb->aio_fildes, 0, SEEK_CUR);
    _lseeki64(cb->aio_fildes, cb->aio_offset, SEEK_SET);
    int n = write ? _write(cb->aio_fildes, cb->aio_buf, (unsigned)cb->aio_nbytes)
                  : _read(cb->aio_fildes, cb->aio_buf, (unsigned)cb->aio_nbytes);
    _lseeki64(cb->aio_fildes, saved, SEEK_SET);
    cb->return_value = n;
    cb->error_code = n < 0 ? errno : 0;
    return 0;
}

static int kt_aio_read(struct linux_aiocb *cb) { return aio_transfer(cb, false); }
static int kt_aio_write(struct linux_aiocb *cb) { return aio_transfer(cb, true); }
static int kt_aio_error(const struct linux_aiocb *cb) { return cb->error_code; }
static int kt_aio_return(struct linux_aiocb *cb) { return cb->return_value; }
static int kt_aio_cancel(int fd, struct linux_aiocb *cb)
{
    (void)fd;
    (void)cb;
    return 2; /* AIO_ALLDONE */
}
static int kt_aio_suspend(const void *list, int count, const void *timeout)
{
    (void)list;
    (void)count;
    (void)timeout;
    return 0;
}

static int kt_adjtimex(void *buf)
{
    (void)buf;
    return 0; /* TIME_OK */
}

#define LINUX_SIGSET_BYTES 128

static int kt_sigemptyset(void *set)
{
    memset(set, 0, LINUX_SIGSET_BYTES);
    return 0;
}

static int kt_sigfillset(void *set)
{
    memset(set, 0xff, LINUX_SIGSET_BYTES);
    return 0;
}

static int kt_sigaddset(void *set, int signo)
{
    if (signo <= 0 || signo > LINUX_SIGSET_BYTES * 8)
    {
        errno = EINVAL;
        return -1;
    }
    ((uint32_t *)set)[(signo - 1) / 32] |= 1u << ((signo - 1) % 32);
    return 0;
}

static int kt_sigaction(int signo, const void *act, void *oldact)
{
    (void)signo;
    (void)act;
    if (oldact)
        memset(oldact, 0, 140); /* sizeof(struct sigaction) on i386 */
    return 0;
}

static void *kt_signal(int signo, void *handler)
{
    (void)signo;
    (void)handler;
    return NULL; /* SIG_DFL */
}

static int kt_sigprocmask(int how, const void *set, void *oldset)
{
    (void)how;
    (void)set;
    if (oldset)
        memset(oldset, 0, LINUX_SIGSET_BYTES);
    return 0;
}

/* ITIMER_REAL for sigwait(): N2's JVS thread runs one bus cycle per SIGALRM. */
#define LINUX_SIGALRM 14

struct linux_timeval
{
    int32_t tv_sec;
    int32_t tv_usec;
};

struct linux_itimerval
{
    struct linux_timeval it_interval;
    struct linux_timeval it_value;
};

static SRWLOCK g_alarmLock = SRWLOCK_INIT;
static int64_t g_alarmDeadline; /* QPC ticks; 0 = disarmed */
static int64_t g_alarmInterval; /* QPC ticks; 0 = one-shot */

static int64_t timeval_to_ticks(const struct linux_timeval *tv)
{
    return ((int64_t)tv->tv_sec * 1000000 + tv->tv_usec) * qpc_frequency() / 1000000;
}

static int kt_setitimer(int which, const struct linux_itimerval *value, struct linux_itimerval *oldValue)
{
    if (oldValue)
        memset(oldValue, 0, sizeof(*oldValue));
    if (which != 0 /* ITIMER_REAL */ || !value)
        return 0;

    AcquireSRWLockExclusive(&g_alarmLock);
    int64_t first = timeval_to_ticks(&value->it_value);
    g_alarmInterval = timeval_to_ticks(&value->it_interval);
    g_alarmDeadline = first ? qpc_now() + first : 0;
    ReleaseSRWLockExclusive(&g_alarmLock);

    log_debug(plfBridges, "setitimer: %d us, then every %d us", value->it_value.tv_usec, value->it_interval.tv_usec);
    return 0;
}

static int kt_sigwait(const void *set, int *sig)
{
    bool wantsAlarm = (((const uint32_t *)set)[(LINUX_SIGALRM - 1) / 32] >> ((LINUX_SIGALRM - 1) % 32)) & 1;

    for (;;)
    {
        AcquireSRWLockExclusive(&g_alarmLock);
        int64_t deadline = g_alarmDeadline;
        ReleaseSRWLockExclusive(&g_alarmLock);

        if (!wantsAlarm || deadline == 0)
        {
            Sleep(50);
            continue;
        }

        int64_t now = qpc_now();
        if (now < deadline)
        {
            int64_t ms = (deadline - now) * 1000 / qpc_frequency();
            Sleep(ms > 1 ? (DWORD)(ms - 1) : 0);
            while (qpc_now() < deadline)
                SwitchToThread();
        }

        AcquireSRWLockExclusive(&g_alarmLock);
        if (g_alarmDeadline == deadline)
        {
            if (g_alarmInterval > 0)
            {
                g_alarmDeadline += g_alarmInterval;
                if (g_alarmDeadline <= qpc_now())
                    g_alarmDeadline = qpc_now() + g_alarmInterval;
            }
            else
            {
                g_alarmDeadline = 0;
            }
        }
        ReleaseSRWLockExclusive(&g_alarmLock);

        if (sig)
            *sig = LINUX_SIGALRM;
        return 0;
    }
}

static void kt_psignal(int sig, const char *s)
{
    log_warn(plfBridges, "%s: signal %d", s ? s : "", sig);
}

static int kt_pthread_kill(unsigned long thread, int sig)
{
    (void)thread;
    (void)sig;
    return 0;
}

static int kt_pthread_setcanceltype(int type, int *oldType)
{
    (void)type;
    if (oldType)
        *oldType = 0;
    return 0;
}

static void kt_pthread_testcancel(void) {}

struct linux_pthread_cleanup_buffer
{
    void (*routine)(void *);
    void *arg;
    int canceltype;
    void *prev;
};

static void kt_pthread_cleanup_push(struct linux_pthread_cleanup_buffer *buffer, void (*routine)(void *), void *arg)
{
    buffer->routine = routine;
    buffer->arg = arg;
    buffer->canceltype = 0;
    buffer->prev = NULL;
}

static void kt_pthread_cleanup_pop(struct linux_pthread_cleanup_buffer *buffer, int execute)
{
    if (execute && buffer->routine)
        buffer->routine(buffer->arg);
}

#define LINUX_MAP_FIXED     0x10
#define LINUX_MAP_ANONYMOUS 0x20
#define LINUX_MAP_FAILED    ((void *)-1)

static void *map_memory(void *addr, size_t length, int flags, int fd, int64_t offset)
{
    if (length == 0)
    {
        errno = EINVAL;
        return LINUX_MAP_FAILED;
    }

    bool anonymous = (flags & LINUX_MAP_ANONYMOUS) || fd < 0;
    if (!anonymous && getDevice(fd))
    {
        errno = ENODEV;
        return LINUX_MAP_FAILED;
    }

    void *request = (flags & LINUX_MAP_FIXED) ? addr : NULL;
    void *mem = VirtualAlloc(request, length, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!mem)
    {
        errno = ENOMEM;
        return LINUX_MAP_FAILED;
    }

    if (!anonymous)
    {
        int64_t saved = _lseeki64(fd, 0, SEEK_CUR);
        _lseeki64(fd, offset, SEEK_SET);
        size_t done = 0;
        while (done < length)
        {
            int n = _read(fd, (char *)mem + done, (unsigned)(length - done > 0x10000000 ? 0x10000000 : length - done));
            if (n <= 0)
                break;
            done += (size_t)n;
        }
        _lseeki64(fd, saved, SEEK_SET);
    }
    return mem;
}

static void *kt_mmap(void *addr, size_t length, int prot, int flags, int fd, int32_t offset)
{
    (void)prot;
    return map_memory(addr, length, flags, fd, (int64_t)(uint32_t)offset);
}

static void *kt_mmap64(void *addr, size_t length, int prot, int flags, int fd, int64_t offset)
{
    (void)prot;
    return map_memory(addr, length, flags, fd, offset);
}

static int kt_munmap(void *addr, size_t length)
{
    (void)length;
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery(addr, &info, sizeof(info)) && info.AllocationBase == addr)
        VirtualFree(addr, 0, MEM_RELEASE);
    return 0;
}

static int kt_ret_zero_mem(void *addr, size_t length)
{
    (void)addr;
    (void)length;
    return 0;
}

/* Alchemy's arena allocator grows through sbrk(). */
static void *kt_sbrk(intptr_t increment)
{
    static const size_t candidates[] = {1024u << 20, 768u << 20, 512u << 20, 256u << 20};
    static SRWLOCK lock = SRWLOCK_INIT;
    static uint8_t *base = NULL;
    static size_t capacity = 0;
    static size_t brk = 0;
    static size_t committed = 0;

    AcquireSRWLockExclusive(&lock);
    if (!base)
    {
        for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]) && !base; ++i)
        {
            base = (uint8_t *)VirtualAlloc(NULL, candidates[i], MEM_RESERVE, PAGE_READWRITE);
            if (base)
            {
                capacity = candidates[i];
                log_debug(plfMemMgr, "sbrk: reserved a %u MB program break", (unsigned)(capacity >> 20));
            }
        }
    }

    if (!base || (increment < 0 && (size_t)(-increment) > brk) ||
        (increment > 0 && (size_t)increment > capacity - brk))
    {
        ReleaseSRWLockExclusive(&lock);
        errno = ENOMEM;
        return (void *)-1;
    }

    uint8_t *previous = base + brk;
    size_t newBrk = brk + increment;
    if (newBrk > committed)
    {
        size_t target = (newBrk + 0xFFFF) & ~(size_t)0xFFFF;
        if (target > capacity)
            target = capacity;
        if (!VirtualAlloc(base + committed, target - committed, MEM_COMMIT, PAGE_READWRITE))
        {
            ReleaseSRWLockExclusive(&lock);
            errno = ENOMEM;
            return (void *)-1;
        }
        committed = target;
    }
    brk = newBrk;
    ReleaseSRWLockExclusive(&lock);
    return previous;
}

#define MAX_SEM_SETS 64
#define LINUX_IPC_CREAT  01000
#define LINUX_IPC_EXCL   02000
#define LINUX_IPC_NOWAIT 04000
#define LINUX_IPC_RMID 0
#define LINUX_IPC_SET  1
#define LINUX_IPC_STAT 2
#define LINUX_GETVAL   12
#define LINUX_GETALL   13
#define LINUX_SETVAL   16
#define LINUX_SETALL   17

typedef struct
{
    bool used;
    int key;
    int count;
    int *values;
} SemSet;

static SemSet g_SemSets[MAX_SEM_SETS];
static SRWLOCK g_SemLock = SRWLOCK_INIT;
static CONDITION_VARIABLE g_SemChanged = CONDITION_VARIABLE_INIT;

static int kt_ftok(const char *path, int id)
{
    uint32_t hash = 2166136261u;
    for (const char *p = path; p && *p; ++p)
        hash = (hash ^ (unsigned char)*p) * 16777619u;
    return (int)(((uint32_t)(id & 0xff) << 24) | (hash & 0xffffff));
}

static int kt_semget(int key, int count, int flags)
{
    AcquireSRWLockExclusive(&g_SemLock);
    int id = -1;
    if (key != 0 /* IPC_PRIVATE */)
    {
        for (int i = 0; i < MAX_SEM_SETS; ++i)
        {
            if (g_SemSets[i].used && g_SemSets[i].key == key)
            {
                id = i;
                break;
            }
        }
        if (id >= 0 && (flags & LINUX_IPC_CREAT) && (flags & LINUX_IPC_EXCL))
        {
            ReleaseSRWLockExclusive(&g_SemLock);
            errno = EEXIST;
            return -1;
        }
    }

    if (id < 0)
    {
        if (key != 0 && !(flags & LINUX_IPC_CREAT))
        {
            ReleaseSRWLockExclusive(&g_SemLock);
            errno = ENOENT;
            return -1;
        }
        for (int i = 0; i < MAX_SEM_SETS; ++i)
        {
            if (!g_SemSets[i].used)
            {
                g_SemSets[i].used = true;
                g_SemSets[i].key = key;
                g_SemSets[i].count = count > 0 ? count : 1;
                g_SemSets[i].values = (int *)calloc((size_t)g_SemSets[i].count, sizeof(int));
                id = i;
                break;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_SemLock);

    if (id < 0)
        errno = ENOSPC;
    return id;
}

struct linux_sembuf
{
    unsigned short sem_num;
    short sem_op;
    short sem_flg;
};

static bool sem_ops_ready(const SemSet *set, const struct linux_sembuf *ops, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        int value = set->values[ops[i].sem_num];
        if (ops[i].sem_op < 0 && value < -ops[i].sem_op)
            return false;
        if (ops[i].sem_op == 0 && value != 0)
            return false;
    }
    return true;
}

static int kt_semop(int id, struct linux_sembuf *ops, size_t count)
{
    if (id < 0 || id >= MAX_SEM_SETS)
    {
        errno = EINVAL;
        return -1;
    }

    AcquireSRWLockExclusive(&g_SemLock);
    SemSet *set = &g_SemSets[id];
    for (size_t i = 0; i < count; ++i)
    {
        if (!set->used || ops[i].sem_num >= set->count)
        {
            ReleaseSRWLockExclusive(&g_SemLock);
            errno = EINVAL;
            return -1;
        }
    }

    while (!sem_ops_ready(set, ops, count))
    {
        bool noWait = false;
        for (size_t i = 0; i < count; ++i)
            noWait |= (ops[i].sem_flg & LINUX_IPC_NOWAIT) != 0;
        if (noWait)
        {
            ReleaseSRWLockExclusive(&g_SemLock);
            errno = EAGAIN;
            return -1;
        }
        SleepConditionVariableSRW(&g_SemChanged, &g_SemLock, INFINITE, 0);
        if (!set->used)
        {
            ReleaseSRWLockExclusive(&g_SemLock);
            errno = EIDRM;
            return -1;
        }
    }

    for (size_t i = 0; i < count; ++i)
        set->values[ops[i].sem_num] += ops[i].sem_op;
    ReleaseSRWLockExclusive(&g_SemLock);
    WakeAllConditionVariable(&g_SemChanged);
    return 0;
}

static int kt_semctl(int id, int semnum, int cmd, ...)
{
    va_list args;
    va_start(args, cmd);
    void *arg = va_arg(args, void *);
    va_end(args);

    if (id < 0 || id >= MAX_SEM_SETS)
    {
        errno = EINVAL;
        return -1;
    }

    AcquireSRWLockExclusive(&g_SemLock);
    SemSet *set = &g_SemSets[id];
    int result = 0;
    if (!set->used)
    {
        errno = EINVAL;
        result = -1;
    }
    else
    {
        switch (cmd & 0xff) /* glibc ORs in IPC_64 */
        {
            case LINUX_IPC_RMID:
                free(set->values);
                memset(set, 0, sizeof(*set));
                break;
            case LINUX_GETVAL:
                result = semnum >= 0 && semnum < set->count ? set->values[semnum] : -1;
                break;
            case LINUX_SETVAL:
                if (semnum >= 0 && semnum < set->count)
                    set->values[semnum] = (int)(intptr_t)arg;
                break;
            case LINUX_GETALL:
                for (int i = 0; i < set->count && arg; ++i)
                    ((unsigned short *)arg)[i] = (unsigned short)set->values[i];
                break;
            case LINUX_SETALL:
                for (int i = 0; i < set->count && arg; ++i)
                    set->values[i] = ((unsigned short *)arg)[i];
                break;
            default: /* IPC_STAT, IPC_SET, GETPID, GETNCNT, GETZCNT */
                break;
        }
    }
    ReleaseSRWLockExclusive(&g_SemLock);
    WakeAllConditionVariable(&g_SemChanged);
    return result;
}

static bool stdin_has_input(void)
{
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == NULL || h == INVALID_HANDLE_VALUE)
        return true;
    switch (GetFileType(h))
    {
        case FILE_TYPE_CHAR:
            return false;
        case FILE_TYPE_PIPE:
        {
            DWORD available = 0;
            if (!PeekNamedPipe(h, NULL, 0, NULL, &available, NULL))
                return true;
            return available != 0;
        }
        default:
            return true;
    }
}

static int kt_IO_getc(FILE *stream)
{
    /* A non-blocking stdin answers EOF when empty, as on Linux. */
    if (stream == stdin && file_stdin_is_nonblocking() && !stdin_has_input())
        return EOF;
    return fgetc(stream);
}
static void kt_clearerr(FILE *stream) { clearerr(stream); }
static int kt_vfscanf(FILE *stream, const char *format, va_list args) { return vfscanf(stream, format, args); }

void libcExtraBridge_initBridges(void)
{
    symbol_register("_setjmp", kt_setjmp, NULL);
    symbol_register("setjmp", kt_setjmp, NULL);
    symbol_register("__sigsetjmp", kt_sigsetjmp, NULL);
    symbol_register("longjmp", kt_longjmp, NULL);
    symbol_register("_longjmp", kt_longjmp, NULL);
    symbol_register("siglongjmp", kt_longjmp, NULL);

    symbol_register("isalnum", kt_isalnum, NULL);
    symbol_register("isalpha", kt_isalpha, NULL);
    symbol_register("iscntrl", kt_iscntrl, NULL);
    symbol_register("islower", kt_islower, NULL);
    symbol_register("isprint", kt_isprint, NULL);
    symbol_register("ispunct", kt_ispunct, NULL);
    symbol_register("isspace", kt_isspace, NULL);
    symbol_register("isupper", kt_isupper, NULL);
    symbol_register("isxdigit", kt_isxdigit, NULL);
    symbol_register("iswalpha", kt_iswalpha, NULL);
    symbol_register("iswcntrl", kt_iswcntrl, NULL);
    symbol_register("iswdigit", kt_iswdigit, NULL);
    symbol_register("iswlower", kt_iswlower, NULL);
    symbol_register("iswprint", kt_iswprint, NULL);
    symbol_register("iswpunct", kt_iswpunct, NULL);
    symbol_register("iswspace", kt_iswspace, NULL);
    symbol_register("iswupper", kt_iswupper, NULL);
    symbol_register("iswxdigit", kt_iswxdigit, NULL);

    symbol_register("wcscmp", kt_wcscmp, NULL);
    symbol_register("wcscpy", kt_wcscpy, NULL);
    symbol_register("wcsncmp", kt_wcsncmp, NULL);
    symbol_register("ceilf", kt_ceilf, NULL);
    symbol_register("lrint", kt_lrint, NULL);
    symbol_register("isdigit", kt_isdigit, NULL);
    symbol_register("syslog", kt_syslog, NULL);
    symbol_register("aio_read", kt_aio_read, NULL);
    symbol_register("aio_write", kt_aio_write, NULL);
    symbol_register("aio_error", kt_aio_error, NULL);
    symbol_register("aio_return", kt_aio_return, NULL);
    symbol_register("aio_cancel", kt_aio_cancel, NULL);
    symbol_register("aio_suspend", kt_aio_suspend, NULL);
    symbol_register("__wcstol_internal", kt_wcstol_internal, NULL);
    symbol_register("swprintf", kt_swprintf, NULL);

    symbol_register("__moddi3", __moddi3, NULL);
    symbol_register("__strtof_internal", kt_strtof_internal, NULL);
    symbol_register("__strtold_internal", kt_strtold_internal, NULL);
    symbol_register("fabsf", kt_fabsf, NULL);
    symbol_register("ldexp", kt_ldexp, NULL);
    symbol_register("log2", kt_log2, NULL);
    symbol_register("isinf", kt_isinf, NULL);
    symbol_register("isnan", kt_isnan, NULL);
    symbol_register("random", kt_random, NULL);
    symbol_register("qsort", qsort, NULL);
    symbol_register("bzero", kt_bzero, NULL);
    symbol_register("strerror_r", kt_strerror_r, NULL);
    symbol_register("localeconv", kt_localeconv, NULL);
    symbol_register("gnu_get_libc_version", kt_gnu_get_libc_version, NULL);

    symbol_register("gmtime", kt_gmtime, NULL);
    symbol_register("gmtime_r", kt_gmtime_r, NULL);
    symbol_register("timegm", kt_timegm, NULL);
    symbol_register("clock_gettime", kt_clock_gettime, NULL);
    symbol_register("clock", kt_clock, NULL);
    symbol_register("setitimer", kt_setitimer, NULL);
    symbol_register("adjtimex", kt_adjtimex, NULL);

    symbol_register("getuid", kt_ret_root, NULL);
    symbol_register("geteuid", kt_ret_root, NULL);
    symbol_register("getgid", kt_ret_root, NULL);
    symbol_register("getppid", kt_getppid, NULL);
    symbol_register("setsid", kt_setsid, NULL);
    symbol_register("setpgid", hook_ret_zero, NULL);
    symbol_register("getpagesize", kt_getpagesize, NULL);
    symbol_register("get_nprocs", kt_get_nprocs, NULL);
    symbol_register("sysconf", kt_sysconf, NULL);
    symbol_register("getpwuid_r", kt_getpwuid_r, NULL);
    symbol_register("execl", kt_execl, NULL);
    symbol_register("waitpid", kt_waitpid, NULL);
    symbol_register("putenv", kt_putenv, NULL);
    symbol_register("setenv", kt_setenv, NULL);
    symbol_register("openlog", kt_openlog, NULL);

    symbol_register("sigemptyset", kt_sigemptyset, NULL);
    symbol_register("sigfillset", kt_sigfillset, NULL);
    symbol_register("sigaddset", kt_sigaddset, NULL);
    symbol_register("sigaction", kt_sigaction, NULL);
    symbol_register("signal", kt_signal, NULL);
    symbol_register("sigprocmask", kt_sigprocmask, NULL);
    symbol_register("pthread_sigmask", kt_sigprocmask, NULL);
    symbol_register("sigwait", kt_sigwait, NULL);
    symbol_register("psignal", kt_psignal, NULL);
    symbol_register("pthread_kill", kt_pthread_kill, NULL);
    symbol_register("pthread_setcanceltype", kt_pthread_setcanceltype, NULL);
    symbol_register("pthread_testcancel", kt_pthread_testcancel, NULL);
    symbol_register("_pthread_cleanup_push", kt_pthread_cleanup_push, NULL);
    symbol_register("_pthread_cleanup_pop", kt_pthread_cleanup_pop, NULL);

    symbol_register("mmap", kt_mmap, NULL);
    symbol_register("mmap64", kt_mmap64, NULL);
    symbol_register("munmap", kt_munmap, NULL);
    symbol_register("msync", kt_ret_zero_mem, NULL);
    symbol_register("mlock", kt_ret_zero_mem, NULL);
    symbol_register("munlock", kt_ret_zero_mem, NULL);
    symbol_register("sbrk", kt_sbrk, NULL);

    symbol_register("ftok", kt_ftok, NULL);
    symbol_register("semget", kt_semget, NULL);
    symbol_register("semop", kt_semop, NULL);
    symbol_register("semctl", kt_semctl, NULL);

    symbol_register("_IO_getc", kt_IO_getc, NULL);
    symbol_register("clearerr", kt_clearerr, NULL);
    symbol_register("vfscanf", kt_vfscanf, NULL);
    symbol_register("_IO_2_1_stdout_", (void *)stdout, NULL);
}
