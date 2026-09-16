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

void fpsLimiter()
{
    LARGE_INTEGER now;
    LONGLONG target_qpc;

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
} 