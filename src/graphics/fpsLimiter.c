#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include "../graphics/fpsLimiter.h"

static LARGE_INTEGER g_qpc_freq;
static LONGLONG g_qpc_start = 0;
static LONGLONG g_frame_idx = 0;
static LONGLONG qpc_per_frame_num;
static LONGLONG qpc_per_frame_den;
static BOOL g_bBusyWait;

void initFpsLimiter(int _fpsNum, int _fpsDen, bool busyWait)
{
    LARGE_INTEGER now;

    QueryPerformanceFrequency(&g_qpc_freq);
    QueryPerformanceCounter(&now);

    /* QPC / frame */
    g_qpc_start = now.QuadPart;
    qpc_per_frame_num = g_qpc_freq.QuadPart * _fpsDen;
    qpc_per_frame_den = _fpsNum;
    g_frame_idx = 0;
    g_bBusyWait = g_bBusyWait;
}

static int g_ftState;
static LONGLONG g_ftLimitStart, g_ftLimitEnd;

void fpsLimiter()
{
    LARGE_INTEGER now;
    LONGLONG target_qpc;

    if (g_ftState == 2)
    {
        QueryPerformanceCounter(&now);
        g_ftLimitStart = now.QuadPart;
    }

    target_qpc = g_qpc_start + (g_frame_idx * qpc_per_frame_num) / qpc_per_frame_den;

    while (1)
    {
        QueryPerformanceCounter(&now);
        LONGLONG diff = target_qpc - now.QuadPart;

        if (diff <= 0)
            break;

        if (g_bBusyWait)
        {
            DWORD ms = (DWORD)(diff * 1000 / g_qpc_freq.QuadPart);

            if (ms > 0)
                Sleep(ms);
            else
                break;
        }
        else
            if (diff > g_qpc_freq.QuadPart / 1000)
                SwitchToThread();
            else
                _mm_pause();
    }

    LONGLONG now_qpc = now.QuadPart;
    LONGLONG expected = g_qpc_start + (g_frame_idx * qpc_per_frame_num) / qpc_per_frame_den;

    if (now_qpc - expected > qpc_per_frame_num / qpc_per_frame_den)
    {
        g_frame_idx = (now_qpc - g_qpc_start) * qpc_per_frame_den / qpc_per_frame_num;
    }

    g_frame_idx++;

    if (g_ftState == 2)
    {
        QueryPerformanceCounter(&now);
        g_ftLimitEnd = now.QuadPart;
    }
}

static FILE *g_ftFile;
static LONGLONG g_ftFreq, g_ftStart, g_ftPrev, g_ftCall, g_ftPrevSwapEnd;

static int frame_time_log_enabled(void)
{
    if (g_ftState == 0)
    {
        const char *path = getenv("KITTEN_FRAMETIME_LOG");
        g_ftFile = (path && *path) ? fopen(path, "w") : NULL;
        if (g_ftFile)
        {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            g_ftFreq = f.QuadPart;
            setvbuf(g_ftFile, NULL, _IOFBF, 1 << 16);
            fputs("present_us,interval_us,swap_us,work_us,wait_us\n", g_ftFile);
        }
        g_ftState = g_ftFile ? 2 : 1;
    }
    return g_ftState == 2;
}

void frameTimeLog_beforeSwap(void)
{
    if (!frame_time_log_enabled())
        return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    g_ftCall = now.QuadPart;
    if (!g_ftStart)
        g_ftStart = g_ftCall;
}

void frameTimeLog_afterSwap(void)
{
    if (g_ftState != 2)
        return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    LONGLONG interval = g_ftPrev ? g_ftCall - g_ftPrev : 0;
    LONGLONG work = (g_ftPrevSwapEnd && g_ftLimitStart > g_ftPrevSwapEnd)
                        ? g_ftLimitStart - g_ftPrevSwapEnd : 0;
    LONGLONG wait = g_ftLimitEnd > g_ftLimitStart ? g_ftLimitEnd - g_ftLimitStart : 0;
    fprintf(g_ftFile, "%lld,%lld,%lld,%lld,%lld\n",
            (g_ftCall - g_ftStart) * 1000000 / g_ftFreq,
            interval * 1000000 / g_ftFreq,
            (now.QuadPart - g_ftCall) * 1000000 / g_ftFreq,
            work * 1000000 / g_ftFreq,
            wait * 1000000 / g_ftFreq);
    g_ftPrev = g_ftCall;
    g_ftPrevSwapEnd = now.QuadPart;
    static unsigned n;
    if (++n % 600 == 0)
        fflush(g_ftFile);
}
